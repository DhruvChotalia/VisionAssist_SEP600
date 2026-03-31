################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../source/audio_player.c \
../source/display_manager.c \
../source/i2c_hal.c \
../source/main.c \
../source/semihost_hardfault.c \
../source/vibration_motor.c \
../source/vision_tof.c \
../source/vl53l0x_api.c \
../source/vl53l0x_api_calibration.c \
../source/vl53l0x_api_core.c \
../source/vl53l0x_api_ranging.c \
../source/vl53l0x_api_strings.c \
../source/vl53l0x_platform.c 

C_DEPS += \
./source/audio_player.d \
./source/display_manager.d \
./source/i2c_hal.d \
./source/main.d \
./source/semihost_hardfault.d \
./source/vibration_motor.d \
./source/vision_tof.d \
./source/vl53l0x_api.d \
./source/vl53l0x_api_calibration.d \
./source/vl53l0x_api_core.d \
./source/vl53l0x_api_ranging.d \
./source/vl53l0x_api_strings.d \
./source/vl53l0x_platform.d 

OBJS += \
./source/audio_player.o \
./source/display_manager.o \
./source/i2c_hal.o \
./source/main.o \
./source/semihost_hardfault.o \
./source/vibration_motor.o \
./source/vision_tof.o \
./source/vl53l0x_api.o \
./source/vl53l0x_api_calibration.o \
./source/vl53l0x_api_core.o \
./source/vl53l0x_api_ranging.o \
./source/vl53l0x_api_strings.o \
./source/vl53l0x_platform.o 


# Each subdirectory must supply rules for building sources it contributes
source/%.o: ../source/%.c source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -D__REDLIB__ -DCPU_MK66FN2M0VMD18 -DCPU_MK66FN2M0VMD18_cm4 -DSDK_OS_BAREMETAL -DSERIAL_PORT_TYPE_UART=1 -DSDK_DEBUGCONSOLE=1 -DCR_INTEGER_PRINTF -DPRINTF_FLOAT_ENABLE=0 -D__MCUXPRESSO -D__USE_CMSIS -DDEBUG -DSDK_OS_FREE_RTOS -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/board" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/drivers" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/component/uart" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/device" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/CMSIS" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/component/serial_manager" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/component/lists" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/utilities" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/freertos/freertos_kernel/include" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/freertos/freertos_kernel/portable/GCC/ARM_CM4F" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/source" -O0 -fno-common -g3 -gdwarf-4 -Wall -c -ffunction-sections -fdata-sections -fno-builtin -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-source

clean-source:
	-$(RM) ./source/audio_player.d ./source/audio_player.o ./source/display_manager.d ./source/display_manager.o ./source/i2c_hal.d ./source/i2c_hal.o ./source/main.d ./source/main.o ./source/semihost_hardfault.d ./source/semihost_hardfault.o ./source/vibration_motor.d ./source/vibration_motor.o ./source/vision_tof.d ./source/vision_tof.o ./source/vl53l0x_api.d ./source/vl53l0x_api.o ./source/vl53l0x_api_calibration.d ./source/vl53l0x_api_calibration.o ./source/vl53l0x_api_core.d ./source/vl53l0x_api_core.o ./source/vl53l0x_api_ranging.d ./source/vl53l0x_api_ranging.o ./source/vl53l0x_api_strings.d ./source/vl53l0x_api_strings.o ./source/vl53l0x_platform.d ./source/vl53l0x_platform.o

.PHONY: clean-source

