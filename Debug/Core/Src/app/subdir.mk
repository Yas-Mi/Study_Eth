################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app/console.c \
../Core/Src/app/eth_send_data.c \
../Core/Src/app/eth_test.c 

OBJS += \
./Core/Src/app/console.o \
./Core/Src/app/eth_send_data.o \
./Core/Src/app/eth_test.o 

C_DEPS += \
./Core/Src/app/console.d \
./Core/Src/app/eth_send_data.d \
./Core/Src/app/eth_test.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/app/%.o Core/Src/app/%.su Core/Src/app/%.cyclo: ../Core/Src/app/%.c Core/Src/app/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F769xx -c -I../Core/Inc -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/middle/microps" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/util" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/app" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/drv" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/peri" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-app

clean-Core-2f-Src-2f-app:
	-$(RM) ./Core/Src/app/console.cyclo ./Core/Src/app/console.d ./Core/Src/app/console.o ./Core/Src/app/console.su ./Core/Src/app/eth_send_data.cyclo ./Core/Src/app/eth_send_data.d ./Core/Src/app/eth_send_data.o ./Core/Src/app/eth_send_data.su ./Core/Src/app/eth_test.cyclo ./Core/Src/app/eth_test.d ./Core/Src/app/eth_test.o ./Core/Src/app/eth_test.su

.PHONY: clean-Core-2f-Src-2f-app

