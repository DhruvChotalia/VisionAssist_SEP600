/**
 * i2c_hal.c — VisionAssist I2C Hardware Abstraction Layer
 * =========================================================
 *
 * Targets I2C0 on PTB2 (SCL) / PTB3 (SDA) for the VL53L0X ToF sensor.
 *
 * KEY FIX — I2C transfer timeout
 * ────────────────────────────────
 * The original code used I2C_MasterTransferBlocking() directly.  That call
 * spins on a status flag with NO timeout.  If the sensor is not yet powered,
 * not responding, or the bus is stuck (SDA held low by a previous incomplete
 * transfer), the function never returns and Motor_Task hangs at priority 3,
 * starving every other task and freezing the system.
 *
 * The fix uses I2C_MasterTransferNonBlocking() + a tick-based deadline loop
 * so that any hung transfer times out after I2C_TRANSFER_TIMEOUT_MS and
 * returns kStatus_I2C_Timeout instead of blocking forever.
 *
 * KEY FIX — I2C bus recovery
 * ───────────────────────────
 * I2C_HAL_BusRecover() bit-bangs 9 SCL pulses on the bus using GPIO mode,
 * then issues a STOP condition.  This releases an SDA line that was held low
 * by the sensor mid-transfer (e.g. after a reset without a clean STOP).
 * Call it once in Motor_Task if VL53L0X_DataInit() fails, then re-init I2C0.
 *
 * Pin assignments (must match your board):
 *   PTB2 = I2C0_SCL
 *   PTB3 = I2C0_SDA
 */

#include "i2c_hal.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_i2c.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"

/* ── Pin definitions for bit-bang recovery ──────────────────────────────── */
#define TOF_SCL_PORT   PORTB
#define TOF_SCL_GPIO   GPIOB
#define TOF_SCL_PIN    2U

#define TOF_SDA_PORT   PORTB
#define TOF_SDA_GPIO   GPIOB
#define TOF_SDA_PIN    3U

/* Timeout for a single I2C transfer (ms) */
#define I2C_TRANSFER_TIMEOUT_MS   50U

/* ── Internal handle for non-blocking transfers ─────────────────────────── */
static i2c_master_handle_t s_i2cHandle;
static volatile bool        s_transferDone  = false;
static volatile status_t    s_transferStatus = kStatus_Success;

static void I2C_MasterCallback(I2C_Type *base,
                                i2c_master_handle_t *handle,
                                status_t status,
                                void *userData)
{
    (void)base; (void)handle; (void)userData;
    s_transferStatus = status;
    s_transferDone   = true;
}

/* =========================================================================
 * I2C_HAL_Init
 * ========================================================================= */
void I2C_HAL_Init(void)
{
    CLOCK_EnableClock(kCLOCK_PortB);
    CLOCK_EnableClock(kCLOCK_I2c0);

    port_pin_config_t pin_config = {
        kPORT_PullUp,
        kPORT_FastSlewRate,
        kPORT_PassiveFilterDisable,
        kPORT_OpenDrainEnable,
        kPORT_LowDriveStrength,
        kPORT_MuxAlt2,
        kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORTB, TOF_SCL_PIN, &pin_config);
    PORT_SetPinConfig(PORTB, TOF_SDA_PIN, &pin_config);

    i2c_master_config_t masterConfig;
    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = 100000U;
    I2C_MasterInit(TOF_I2C_BASE, &masterConfig, CLOCK_GetFreq(kCLOCK_BusClk));

    /* Create the non-blocking transfer handle once */
    I2C_MasterTransferCreateHandle(TOF_I2C_BASE, &s_i2cHandle,
                                   I2C_MasterCallback, NULL);
}

/* =========================================================================
 * I2C_HAL_BusRecover
 *
 * Call when VL53L0X_DataInit() fails on first attempt.
 * Switches PTB2/PTB3 to GPIO output, clocks 9 SCL pulses to shift out any
 * stuck byte the sensor is mid-transmitting, then issues a STOP, then
 * restores Alt2 (I2C) mux so normal transfers can resume.
 * ========================================================================= */
void I2C_HAL_BusRecover(void)
{
    PRINTF("[I2C_HAL] Bus recovery: sending 9 clock pulses.\r\n");

    /* Disable I2C peripheral so we can GPIO-drive the pins */
    I2C_MasterDeinit(TOF_I2C_BASE);

    /* Switch pins to GPIO output, open-drain implied by external pull-ups */
    port_pin_config_t gpio_cfg = {
        kPORT_PullUp,
        kPORT_FastSlewRate,
        kPORT_PassiveFilterDisable,
        kPORT_OpenDrainEnable,
        kPORT_LowDriveStrength,
        kPORT_MuxAsGpio,
        kPORT_UnlockRegister
    };
    PORT_SetPinConfig(TOF_SCL_PORT, TOF_SCL_PIN, &gpio_cfg);
    PORT_SetPinConfig(TOF_SDA_PORT, TOF_SDA_PIN, &gpio_cfg);

    gpio_pin_config_t out_high = { kGPIO_DigitalOutput, 1U };
    GPIO_PinInit(TOF_SCL_GPIO, TOF_SCL_PIN, &out_high);
    GPIO_PinInit(TOF_SDA_GPIO, TOF_SDA_PIN, &out_high);

    /* Simple bit-bang delay: ~5 µs at 120 MHz core */
    #define BB_DELAY() do { \
        for (volatile uint32_t _d = 0; _d < 600; _d++) { __asm("NOP"); } \
    } while(0)

    /* Clock 9 SCL pulses — enough to clock out any stuck byte */
    for (int i = 0; i < 9; i++) {
        GPIO_PinWrite(TOF_SCL_GPIO, TOF_SCL_PIN, 0U);  /* SCL low  */
        BB_DELAY();
        GPIO_PinWrite(TOF_SCL_GPIO, TOF_SCL_PIN, 1U);  /* SCL high */
        BB_DELAY();

        /* If SDA went high the device released the bus — done early */
        if (GPIO_PinRead(TOF_SDA_GPIO, TOF_SDA_PIN)) {
            PRINTF("[I2C_HAL] SDA released after %d pulses.\r\n", i + 1);
            break;
        }
    }

    /* Issue a STOP condition: SDA low→high while SCL high */
    GPIO_PinWrite(TOF_SDA_GPIO, TOF_SDA_PIN, 0U);
    BB_DELAY();
    GPIO_PinWrite(TOF_SCL_GPIO, TOF_SCL_PIN, 1U);
    BB_DELAY();
    GPIO_PinWrite(TOF_SDA_GPIO, TOF_SDA_PIN, 1U);
    BB_DELAY();

    PRINTF("[I2C_HAL] Bus recovery complete. SDA=%u\r\n",
           GPIO_PinRead(TOF_SDA_GPIO, TOF_SDA_PIN));

    /* Restore Alt2 (I2C) mux and re-initialise the peripheral */
    port_pin_config_t i2c_cfg = {
        kPORT_PullUp,
        kPORT_FastSlewRate,
        kPORT_PassiveFilterDisable,
        kPORT_OpenDrainEnable,
        kPORT_LowDriveStrength,
        kPORT_MuxAlt2,
        kPORT_UnlockRegister
    };
    PORT_SetPinConfig(TOF_SCL_PORT, TOF_SCL_PIN, &i2c_cfg);
    PORT_SetPinConfig(TOF_SDA_PORT, TOF_SDA_PIN, &i2c_cfg);

    i2c_master_config_t masterConfig;
    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = 100000U;
    I2C_MasterInit(TOF_I2C_BASE, &masterConfig, CLOCK_GetFreq(kCLOCK_BusClk));

    I2C_MasterTransferCreateHandle(TOF_I2C_BASE, &s_i2cHandle,
                                   I2C_MasterCallback, NULL);
}

/* =========================================================================
 * Internal: timeout-guarded non-blocking transfer
 *
 * Starts a non-blocking transfer and polls s_transferDone with a
 * tick-based deadline.  Returns kStatus_I2C_Timeout if the sensor
 * does not respond within I2C_TRANSFER_TIMEOUT_MS.
 *
 * Uses taskYIELD() while waiting so other tasks (Comm, Button) can run
 * even if the ToF sensor is slow to respond.
 * ========================================================================= */
static status_t I2C_TransferWithTimeout(i2c_master_transfer_t *xfer)
{
    s_transferDone   = false;
    s_transferStatus = kStatus_Success;

    status_t startStatus = I2C_MasterTransferNonBlocking(TOF_I2C_BASE,
                                                          &s_i2cHandle, xfer);
    if (startStatus != kStatus_Success) {
        return startStatus;
    }

    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(I2C_TRANSFER_TIMEOUT_MS);

    while (!s_transferDone) {
        if (xTaskGetTickCount() >= deadline) {
            /* Abort the hung transfer */
            I2C_MasterTransferAbort(TOF_I2C_BASE, &s_i2cHandle);
            PRINTF("[I2C_HAL] Transfer timeout!\r\n");
            return kStatus_I2C_Timeout;
        }
        taskYIELD();
    }

    return s_transferStatus;
}

/* =========================================================================
 * I2C_HAL_ReadRegs
 * ========================================================================= */
status_t I2C_HAL_ReadRegs(uint8_t deviceAddr, uint8_t regAddr,
                           uint8_t *rxBuff, uint32_t rxSize)
{
    i2c_master_transfer_t xfer = {0};
    xfer.slaveAddress   = deviceAddr;
    xfer.direction      = kI2C_Read;
    xfer.subaddress     = regAddr;
    xfer.subaddressSize = 1;
    xfer.data           = rxBuff;
    xfer.dataSize       = rxSize;
    xfer.flags          = kI2C_TransferDefaultFlag;

    return I2C_TransferWithTimeout(&xfer);
}

/* =========================================================================
 * I2C_HAL_WriteRegs
 * ========================================================================= */
status_t I2C_HAL_WriteRegs(uint8_t deviceAddr, uint8_t regAddr,
                            uint8_t *txBuff, uint32_t txSize)
{
    i2c_master_transfer_t xfer = {0};
    xfer.slaveAddress   = deviceAddr;
    xfer.direction      = kI2C_Write;
    xfer.subaddress     = regAddr;
    xfer.subaddressSize = 1;
    xfer.data           = txBuff;
    xfer.dataSize       = txSize;
    xfer.flags          = kI2C_TransferDefaultFlag;

    return I2C_TransferWithTimeout(&xfer);
}
