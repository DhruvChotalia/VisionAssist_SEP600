#ifndef I2C_HAL_H
#define I2C_HAL_H

#include "fsl_common.h"
#include "fsl_i2c.h"

#define TOF_I2C_BASE I2C0

void I2C_HAL_Init(void);
status_t I2C_HAL_ReadRegs(uint8_t deviceAddr, uint8_t regAddr, uint8_t *rxBuff, uint32_t rxSize);
status_t I2C_HAL_WriteRegs(uint8_t deviceAddr, uint8_t regAddr, uint8_t *txBuff, uint32_t txSize);

#endif /* I2C_HAL_H */
