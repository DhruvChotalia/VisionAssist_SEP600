/**
 * i2c_hal.h — VisionAssist I2C Hardware Abstraction Layer
 * =========================================================
 *
 * Wraps NXP SDK I2C master transfers for the VL53L0X ToF sensor (I2C0).
 *
 * Added: I2C_HAL_BusRecover() — call once at startup if VL53L0X_DataInit
 * fails, to clear a stuck SDA/SCL line by bit-banging 9 clock pulses.
 */

#ifndef I2C_HAL_H
#define I2C_HAL_H

#include "fsl_common.h"
#include "fsl_i2c.h"

#define TOF_I2C_BASE I2C0

void     I2C_HAL_Init(void);
void     I2C_HAL_BusRecover(void);   /* bit-bang 9 clocks to unstick SDA */

status_t I2C_HAL_ReadRegs (uint8_t deviceAddr, uint8_t regAddr,
                            uint8_t *rxBuff, uint32_t rxSize);
status_t I2C_HAL_WriteRegs(uint8_t deviceAddr, uint8_t regAddr,
                            uint8_t *txBuff, uint32_t txSize);

#endif /* I2C_HAL_H */
