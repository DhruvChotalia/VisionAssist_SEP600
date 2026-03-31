/**
 * main.c — VisionAssist K66F Master Controller
 * ==============================================
 * Hardware init + FreeRTOS scheduler entry point.
 *
 * Global mutexes created here (before any task) so vision_tof.c tasks
 * can safely take them at any time without a race on startup.
 *
 * UART1 baud rate: 115200.  Must match BAUD_RATE in esp32_visionassist.ino.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_uart.h"

#include "i2c_hal.h"
#include "vision_tof.h"
#include "vibration_motor.h"
#include "audio_player.h"
#include "display_manager.h"

/* ── UART to ESP32 ────────────────────────────────────────────────────────── */
#define ESP_UART          UART1
#define ESP_UART_CLK_FREQ CLOCK_GetFreq(kCLOCK_CoreSysClk)
#define ESP_UART_BAUD     115200U   /* Must match BAUD_RATE in ESP32 firmware */

/* ── Global mutex handles (extern-declared in vision_tof.h) ─────────────── */
SemaphoreHandle_t xDisplayMutex = NULL;
SemaphoreHandle_t xAudioMutex   = NULL;
SemaphoreHandle_t xUartTxMutex  = NULL;
/* =========================================================================
 * Hardware Initialisation
 * ========================================================================= */
static void InitHardware(void)
{
    /* ── Clocks ──────────────────────────────────────────────────────────── */
    CLOCK_EnableClock(kCLOCK_PortA);
    CLOCK_EnableClock(kCLOCK_PortC);
    CLOCK_EnableClock(kCLOCK_Uart1);

    /* ── UART1 pins: PTC3 = RX, PTC4 = TX (Alt3) ────────────────────────── */
    PORT_SetPinMux(PORTC, 3U, kPORT_MuxAlt3);   /* UART1_RX */
    PORT_SetPinMux(PORTC, 4U, kPORT_MuxAlt3);   /* UART1_TX */

    /* ── Button GPIOs: PTA1, PTC16 with pull-down ────────────────────────── */
    port_pin_config_t btn_cfg = {
        .pullSelect          = kPORT_PullDown,
        .slewRate            = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterEnable,
        .openDrainEnable     = kPORT_OpenDrainDisable,
        .driveStrength       = kPORT_LowDriveStrength,
        .mux                 = kPORT_MuxAsGpio,
        .lockRegister        = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORTA, 1U,  &btn_cfg);
    PORT_SetPinConfig(PORTC, 16U, &btn_cfg);

    gpio_pin_config_t gpio_in = { kGPIO_DigitalInput, 0 };
    GPIO_PinInit(GPIOA, 1U,  &gpio_in);
    GPIO_PinInit(GPIOC, 16U, &gpio_in);

    /* ── UART1 @ 115200 8N1 ──────────────────────────────────────────────── */
    uart_config_t uart_cfg;
    UART_GetDefaultConfig(&uart_cfg);
    uart_cfg.baudRate_Bps = ESP_UART_BAUD;
    uart_cfg.enableTx     = true;
    uart_cfg.enableRx     = true;
    UART_Init(ESP_UART, &uart_cfg, ESP_UART_CLK_FREQ);
}

/* =========================================================================
 * Main Entry
 * ========================================================================= */
int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    PRINTF("\r\n==============================================\r\n");
    PRINTF("  VisionAssist K66F Controller  --  Booting\r\n");
    PRINTF("==============================================\r\n");

    InitHardware();
    I2C_HAL_Init();
    Vibration_Motor_Init();
    Audio_Player_Init();
    Display_Init();

    /* ── Create mutexes BEFORE tasks so no task can take a NULL handle ───── */
    xDisplayMutex = xSemaphoreCreateMutex();
    xAudioMutex   = xSemaphoreCreateMutex();
    xUartTxMutex = xSemaphoreCreateMutex();
    if (xDisplayMutex == NULL || xAudioMutex == NULL || xUartTxMutex == NULL ) {
        PRINTF("[FATAL] Mutex creation failed — halting.\r\n");
        while (1);
    }

    /* ── Spawn application tasks ─────────────────────────────────────────── */
    Vision_ToF_Init();

    PRINTF("[BOOT] Starting FreeRTOS scheduler...\r\n");
    vTaskStartScheduler();

    /* Should never reach here */
    for (;;);
    return 0;
}
