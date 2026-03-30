#ifndef VIBRATION_MOTOR_H
#define VIBRATION_MOTOR_H

#include <stdint.h>

void Vibration_Motor_Init(void);
void Vibration_Motor_SetIntensity(uint16_t distance, uint16_t threshold);
void Vibration_Motor_Off(void);

#endif /* VIBRATION_MOTOR_H */
