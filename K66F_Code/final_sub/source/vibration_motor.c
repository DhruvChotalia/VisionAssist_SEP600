#include "vibration_motor.h"
#include "fsl_ftm.h"
#include "fsl_port.h"
#include "clock_config.h"
#include "fsl_debug_console.h"

#define VIBRATION_FTM          FTM3
#define VIBRATION_CHANNEL      kFTM_Chnl_4
#define VIBRATION_PORT         PORTC
#define VIBRATION_PIN          8U
#define VIBRATION_ALT          kPORT_MuxAlt3

#define PWM_FREQUENCY_HZ       1000U
#define MIN_DUTY_PERCENT       5U
#define MAX_DUTY_PERCENT       100U

void Vibration_Motor_Init(void) {
    CLOCK_EnableClock(kCLOCK_PortC);
    PORT_SetPinMux(VIBRATION_PORT, VIBRATION_PIN, VIBRATION_ALT);

    ftm_config_t ftmConfig;
    FTM_GetDefaultConfig(&ftmConfig);
    ftmConfig.prescale = kFTM_Prescale_Divide_4;
    FTM_Init(VIBRATION_FTM, &ftmConfig);

    ftm_chnl_pwm_signal_param_t pwmParam;
    pwmParam.chnlNumber        = VIBRATION_CHANNEL;
    pwmParam.level             = kFTM_HighTrue;
    pwmParam.dutyCyclePercent  = 0U;
    pwmParam.firstEdgeDelayPercent = 0U;

    FTM_SetupPwm(VIBRATION_FTM, &pwmParam, 1U,
                 kFTM_EdgeAlignedPwm, PWM_FREQUENCY_HZ,
                 CLOCK_GetFreq(kCLOCK_BusClk));

    FTM_StartTimer(VIBRATION_FTM, kFTM_SystemClock);
    PRINTF("[MOTOR] PWM initialized on PTC8 (FTM3_CH4) at 0%% duty\r\n");
}

void Vibration_Motor_SetIntensity(uint16_t distance, uint16_t threshold) {
    uint8_t duty;

    if (distance >= threshold) {
        duty = 0U;
    } else if (distance == 0U) {
        duty = MAX_DUTY_PERCENT;
    } else {
        duty = MAX_DUTY_PERCENT -
               (uint8_t)(((uint32_t)distance * (MAX_DUTY_PERCENT - MIN_DUTY_PERCENT)) / threshold);
    }

    FTM_UpdatePwmDutycycle(VIBRATION_FTM, VIBRATION_CHANNEL,
                           kFTM_EdgeAlignedPwm, duty);
    FTM_SetSoftwareTrigger(VIBRATION_FTM, true);
}

void Vibration_Motor_Off(void) {
    FTM_UpdatePwmDutycycle(VIBRATION_FTM, VIBRATION_CHANNEL,
                           kFTM_EdgeAlignedPwm, 0U);
    FTM_SetSoftwareTrigger(VIBRATION_FTM, true);
}
