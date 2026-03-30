#include "FreeRTOS.h"
#include "task.h"
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
#include "display_manager.h" /* <-- Added Display Manager */

#define ESP_UART UART1
#define ESP_UART_CLK_FREQ CLOCK_GetFreq(kCLOCK_CoreSysClk)

void InitHardware(void) {
    /* 1. Enable clocks */
    CLOCK_EnableClock(kCLOCK_PortA); /* PTA1 for button */
    CLOCK_EnableClock(kCLOCK_PortC); /* PTC16 for button, PTC3/4 for UART */
    CLOCK_EnableClock(kCLOCK_Uart1);

    /* 2. UART Pin Muxing (ESP32 Communication) */
    PORT_SetPinMux(PORTC, 3U, kPORT_MuxAlt3);
    PORT_SetPinMux(PORTC, 4U, kPORT_MuxAlt3);

    /* 3. Button Pin Muxing & Config (Internal Pull-down enabled) */
    port_pin_config_t button_config = {
        kPORT_PullDown,              /* Pull-down resistor enabled */
        kPORT_FastSlewRate,
        kPORT_PassiveFilterEnable,   /* Helps with hardware debouncing */
        kPORT_OpenDrainDisable,
        kPORT_LowDriveStrength,
        kPORT_MuxAsGpio,             /* Set as GPIO */
        kPORT_UnlockRegister
    };

    PORT_SetPinConfig(PORTA, 1U, &button_config);  /* PTA1 */
    PORT_SetPinConfig(PORTC, 16U, &button_config); /* PTC16 */

    /* 4. Set Pins as Digital Inputs */
    gpio_pin_config_t gpio_input = { kGPIO_DigitalInput, 0 };
    GPIO_PinInit(GPIOA, 1U, &gpio_input);
    GPIO_PinInit(GPIOC, 16U, &gpio_input);

    /* 5. Initialize UART */
    uart_config_t uartConfig;
    UART_GetDefaultConfig(&uartConfig);
    uartConfig.baudRate_Bps = 460800;
    uartConfig.enableTx = true;
    uartConfig.enableRx = true;
    UART_Init(ESP_UART, &uartConfig, ESP_UART_CLK_FREQ);
}

int main(void) {
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    PRINTF("\r\n===============================================\r\n");
    PRINTF(" VisionAssist Integrated Controller Started\r\n");
    PRINTF("===============================================\r\n");

    InitHardware();
    I2C_HAL_Init();
    Vibration_Motor_Init();
    Audio_Player_Init();
    Vision_ToF_Init();

    Display_Init(); /* <-- Added Display Initialization */

    PRINTF("Starting FreeRTOS Scheduler...\r\n");

    vTaskStartScheduler();

    for (;;);
    return 0;
}
