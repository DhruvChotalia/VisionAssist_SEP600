################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../freertos/freertos_kernel/portable/MemMang/heap_4.c 

C_DEPS += \
./freertos/freertos_kernel/portable/MemMang/heap_4.d 

OBJS += \
./freertos/freertos_kernel/portable/MemMang/heap_4.o 


# Each subdirectory must supply rules for building sources it contributes
freertos/freertos_kernel/portable/MemMang/%.o: ../freertos/freertos_kernel/portable/MemMang/%.c freertos/freertos_kernel/portable/MemMang/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -D__REDLIB__ -DCPU_MK66FN2M0VMD18 -DCPU_MK66FN2M0VMD18_cm4 -DSDK_OS_BAREMETAL -DSERIAL_PORT_TYPE_UART=1 -DSDK_DEBUGCONSOLE=1 -DCR_INTEGER_PRINTF -DPRINTF_FLOAT_ENABLE=0 -D__MCUXPRESSO -D__USE_CMSIS -DDEBUG -DSDK_OS_FREE_RTOS -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/board" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/drivers" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/component/uart" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/device" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/CMSIS" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/component/serial_manager" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/component/lists" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/utilities" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/freertos/freertos_kernel/include" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/freertos/freertos_kernel/portable/GCC/ARM_CM4F" -I"/Users/krishpatel/Downloads/Projectmain  Button +lcd +filtering + speaker less delay/source" -O0 -fno-common -g3 -gdwarf-4 -Wall -c -ffunction-sections -fdata-sections -fno-builtin -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-freertos-2f-freertos_kernel-2f-portable-2f-MemMang

clean-freertos-2f-freertos_kernel-2f-portable-2f-MemMang:
	-$(RM) ./freertos/freertos_kernel/portable/MemMang/heap_4.d ./freertos/freertos_kernel/portable/MemMang/heap_4.o

.PHONY: clean-freertos-2f-freertos_kernel-2f-portable-2f-MemMang

