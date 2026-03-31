#include "display_manager.h"
#include "board.h"
#include "fsl_i2c.h"
#include "fsl_port.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include <stdio.h>
#include <string.h>

#define I2C_BASEADDR    I2C1
#define I2C_BAUDRATE    100000U
#define LCD_I2C_ADDR    0x27

#define LCD_BACKLIGHT   0x08
#define LCD_EN          0x04
#define LCD_RS          0x01

/* Hardware delay used for initialization before RTOS starts */
static void hw_delay_ms(uint32_t ms) {
    volatile uint32_t i = 0;
    for (i = 0; i < ms * 12000; ++i) { __asm("NOP"); }
}

/* --- Low-Level I2C & LCD Functions --- */

static void Init_I2C1_Pins(void) {
    CLOCK_EnableClock(kCLOCK_PortC);
    port_pin_config_t i2c_pin_config = {
        .pullSelect = kPORT_PullUp,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainEnable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt2,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORTC, 10U, &i2c_pin_config); /* PTC10 = I2C1_SCL */
    PORT_SetPinConfig(PORTC, 11U, &i2c_pin_config); /* PTC11 = I2C1_SDA */
}

static void Init_I2C1_Peripheral(void) {
    i2c_master_config_t masterConfig;
    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = I2C_BAUDRATE;
    /* Using BusClk for K66F I2C1 */
    I2C_MasterInit(I2C_BASEADDR, &masterConfig, CLOCK_GetFreq(kCLOCK_BusClk));
}

static status_t I2C_WriteByte(uint8_t data) {
    i2c_master_transfer_t masterXfer = {0};
    masterXfer.slaveAddress = LCD_I2C_ADDR;
    masterXfer.direction = kI2C_Write;
    masterXfer.data = &data;
    masterXfer.dataSize = 1;
    return I2C_MasterTransferBlocking(I2C_BASEADDR, &masterXfer);
}

static void LCD_WriteNibble(uint8_t nibble, uint8_t rs_mode) {
    uint8_t data = (nibble << 4) | LCD_BACKLIGHT | rs_mode;
    I2C_WriteByte(data | LCD_EN);
    hw_delay_ms(1);
    I2C_WriteByte(data & ~LCD_EN);
    hw_delay_ms(1);
}

static void LCD_SendCommand(uint8_t cmd) {
    LCD_WriteNibble(cmd >> 4, 0);
    LCD_WriteNibble(cmd & 0x0F, 0);
}

static void LCD_SendData(uint8_t data) {
    LCD_WriteNibble(data >> 4, LCD_RS);
    LCD_WriteNibble(data & 0x0F, LCD_RS);
}

static void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    LCD_SendCommand(0x80 | (col + row_offsets[row]));
}

static void LCD_PrintString(const char* str) {
    while (*str) { LCD_SendData((uint8_t)(*str++)); }
}

/* --- Public API --- */

void Display_Init(void) {
    Init_I2C1_Pins();
    Init_I2C1_Peripheral();

    hw_delay_ms(50);
    LCD_WriteNibble(0x03, 0); hw_delay_ms(5);
    LCD_WriteNibble(0x03, 0); hw_delay_ms(1);
    LCD_WriteNibble(0x03, 0); hw_delay_ms(1);
    LCD_WriteNibble(0x02, 0); hw_delay_ms(1);

    LCD_SendCommand(0x28); /* 4-bit, 2 line, 5x8 */
    LCD_SendCommand(0x0C); /* Display ON, Cursor OFF */
    LCD_SendCommand(0x06); /* Entry mode auto-increment */
    LCD_SendCommand(0x01); /* Clear display */
    hw_delay_ms(3);

    PRINTF("[DISPLAY] I2C LCD Initialized.\r\n");

    /* Show default state on boot */
    Display_UpdateScreen("System Status:", "STANDBY");
}

void Display_UpdateScreen(const char* line1, const char* line2) {
    char padded_line1[17];
    char padded_line2[17];

    /* Pad both strings with spaces so they overwrite old characters
       without needing to clear the whole screen (which causes flickering) */
    snprintf(padded_line1, 17, "%-16s", line1);
    snprintf(padded_line2, 17, "%-16s", line2);

    LCD_SetCursor(0, 0);
    LCD_PrintString(padded_line1);

    LCD_SetCursor(1, 0);
    LCD_PrintString(padded_line2);
}
