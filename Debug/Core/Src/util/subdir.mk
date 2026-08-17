################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/util/util.c 

OBJS += \
./Core/Src/util/util.o 

C_DEPS += \
./Core/Src/util/util.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/util/%.o Core/Src/util/%.su Core/Src/util/%.cyclo: ../Core/Src/util/%.c Core/Src/util/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F769xx -c -I../Core/Inc -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/middle/microps" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/util" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/app" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/drv" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/peri" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-util

clean-Core-2f-Src-2f-util:
	-$(RM) ./Core/Src/util/util.cyclo ./Core/Src/util/util.d ./Core/Src/util/util.o ./Core/Src/util/util.su

.PHONY: clean-Core-2f-Src-2f-util

