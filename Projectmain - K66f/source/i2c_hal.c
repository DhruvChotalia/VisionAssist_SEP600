/* * GenAI Declaration:
 * Claude was used to generate the I2C read/write logic and register mapping
 * in the following
 */

#include "i2c_hal.h"
#include "fsl_port.h"

void I2C_HAL_Init(void) {
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

    PORT_SetPinConfig(PORTB, 2U, &pin_config);
    PORT_SetPinConfig(PORTB, 3U, &pin_config);

    i2c_master_config_t masterConfig;
    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = 100000U;

    I2C_MasterInit(TOF_I2C_BASE, &masterConfig, CLOCK_GetFreq(kCLOCK_BusClk));
}

status_t I2C_HAL_ReadRegs(uint8_t deviceAddr, uint8_t regAddr, uint8_t *rxBuff, uint32_t rxSize) {
    i2c_master_transfer_t masterXfer = {0};
    masterXfer.slaveAddress   = deviceAddr;
    masterXfer.direction      = kI2C_Read;
    masterXfer.subaddress     = regAddr;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = rxBuff;
    masterXfer.dataSize       = rxSize;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    return I2C_MasterTransferBlocking(TOF_I2C_BASE, &masterXfer);
}

status_t I2C_HAL_WriteRegs(uint8_t deviceAddr, uint8_t regAddr, uint8_t *txBuff, uint32_t txSize) {
    i2c_master_transfer_t masterXfer = {0};
    masterXfer.slaveAddress   = deviceAddr;
    masterXfer.direction      = kI2C_Write;
    masterXfer.subaddress     = regAddr;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = txBuff;
    masterXfer.dataSize       = txSize;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    return I2C_MasterTransferBlocking(TOF_I2C_BASE, &masterXfer);
}
