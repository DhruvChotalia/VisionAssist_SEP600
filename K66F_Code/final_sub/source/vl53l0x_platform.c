#include "vl53l0x_platform.h"
#include "vl53l0x_api.h"
#include "i2c_hal.h"
#include "FreeRTOS.h"
#include "task.h"

// --- I2C Block Read/Write ---
VL53L0X_Error VL53L0X_WriteMulti(VL53L0X_DEV Dev, uint8_t index, uint8_t *pdata, uint32_t count) {
    status_t status = I2C_HAL_WriteRegs(Dev->I2cDevAddr, index, pdata, count);
    return (status == 0) ? VL53L0X_ERROR_NONE : VL53L0X_ERROR_CONTROL_INTERFACE;
}

VL53L0X_Error VL53L0X_ReadMulti(VL53L0X_DEV Dev, uint8_t index, uint8_t *pdata, uint32_t count) {
    status_t status = I2C_HAL_ReadRegs(Dev->I2cDevAddr, index, pdata, count);
    return (status == 0) ? VL53L0X_ERROR_NONE : VL53L0X_ERROR_CONTROL_INTERFACE;
}

// --- Single Byte/Word Writes ---
VL53L0X_Error VL53L0X_WrByte(VL53L0X_DEV Dev, uint8_t index, uint8_t data) {
    return VL53L0X_WriteMulti(Dev, index, &data, 1);
}

VL53L0X_Error VL53L0X_WrWord(VL53L0X_DEV Dev, uint8_t index, uint16_t data) {
    uint8_t buf[2];
    buf[0] = data >> 8;
    buf[1] = data & 0xFF;
    return VL53L0X_WriteMulti(Dev, index, buf, 2);
}

VL53L0X_Error VL53L0X_WrDWord(VL53L0X_DEV Dev, uint8_t index, uint32_t data) {
    uint8_t buf[4];
    buf[0] = (data >> 24) & 0xFF;
    buf[1] = (data >> 16) & 0xFF;
    buf[2] = (data >> 8) & 0xFF;
    buf[3] = data & 0xFF;
    return VL53L0X_WriteMulti(Dev, index, buf, 4);
}

// --- Single Byte/Word Reads ---
VL53L0X_Error VL53L0X_RdByte(VL53L0X_DEV Dev, uint8_t index, uint8_t *data) {
    return VL53L0X_ReadMulti(Dev, index, data, 1);
}

VL53L0X_Error VL53L0X_RdWord(VL53L0X_DEV Dev, uint8_t index, uint16_t *data) {
    uint8_t buf[2];
    VL53L0X_Error status = VL53L0X_ReadMulti(Dev, index, buf, 2);
    if (status == VL53L0X_ERROR_NONE) {
        *data = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return status;
}

VL53L0X_Error VL53L0X_RdDWord(VL53L0X_DEV Dev, uint8_t index, uint32_t *data) {
    uint8_t buf[4];
    VL53L0X_Error status = VL53L0X_ReadMulti(Dev, index, buf, 4);
    if (status == VL53L0X_ERROR_NONE) {
        *data = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    }
    return status;
}

// --- Utilities ---
VL53L0X_Error VL53L0X_UpdateByte(VL53L0X_DEV Dev, uint8_t index, uint8_t AndData, uint8_t OrData) {
    uint8_t data;
    VL53L0X_Error status = VL53L0X_RdByte(Dev, index, &data);
    if (status != VL53L0X_ERROR_NONE) return status;
    data = (data & AndData) | OrData;
    return VL53L0X_WrByte(Dev, index, data);
}

// Routes ST's delay requests to FreeRTOS
VL53L0X_Error VL53L0X_PollingDelay(VL53L0X_DEV Dev) {
    vTaskDelay(pdMS_TO_TICKS(2));
    return VL53L0X_ERROR_NONE;
}

VL53L0X_Error VL53L0X_LockInterrupts(void) {
    return VL53L0X_ERROR_NONE;
}

VL53L0X_Error VL53L0X_UnlockInterrupts(void) {
    return VL53L0X_ERROR_NONE;
}
