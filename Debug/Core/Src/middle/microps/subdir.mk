################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/middle/microps/arp.c \
../Core/Src/middle/microps/icmp.c \
../Core/Src/middle/microps/ip.c \
../Core/Src/middle/microps/loopback.c \
../Core/Src/middle/microps/net.c \
../Core/Src/middle/microps/netif.c \
../Core/Src/middle/microps/wrap.c 

OBJS += \
./Core/Src/middle/microps/arp.o \
./Core/Src/middle/microps/icmp.o \
./Core/Src/middle/microps/ip.o \
./Core/Src/middle/microps/loopback.o \
./Core/Src/middle/microps/net.o \
./Core/Src/middle/microps/netif.o \
./Core/Src/middle/microps/wrap.o 

C_DEPS += \
./Core/Src/middle/microps/arp.d \
./Core/Src/middle/microps/icmp.d \
./Core/Src/middle/microps/ip.d \
./Core/Src/middle/microps/loopback.d \
./Core/Src/middle/microps/net.d \
./Core/Src/middle/microps/netif.d \
./Core/Src/middle/microps/wrap.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/middle/microps/%.o Core/Src/middle/microps/%.su Core/Src/middle/microps/%.cyclo: ../Core/Src/middle/microps/%.c Core/Src/middle/microps/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F769xx -c -I../Core/Inc -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/middle/microps" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/util" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/app" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/drv" -I"C:/Users/hcuym/OneDrive/Desktop/project/Ethernet/Study_Eth/Core/Src/peri" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-middle-2f-microps

clean-Core-2f-Src-2f-middle-2f-microps:
	-$(RM) ./Core/Src/middle/microps/arp.cyclo ./Core/Src/middle/microps/arp.d ./Core/Src/middle/microps/arp.o ./Core/Src/middle/microps/arp.su ./Core/Src/middle/microps/icmp.cyclo ./Core/Src/middle/microps/icmp.d ./Core/Src/middle/microps/icmp.o ./Core/Src/middle/microps/icmp.su ./Core/Src/middle/microps/ip.cyclo ./Core/Src/middle/microps/ip.d ./Core/Src/middle/microps/ip.o ./Core/Src/middle/microps/ip.su ./Core/Src/middle/microps/loopback.cyclo ./Core/Src/middle/microps/loopback.d ./Core/Src/middle/microps/loopback.o ./Core/Src/middle/microps/loopback.su ./Core/Src/middle/microps/net.cyclo ./Core/Src/middle/microps/net.d ./Core/Src/middle/microps/net.o ./Core/Src/middle/microps/net.su ./Core/Src/middle/microps/netif.cyclo ./Core/Src/middle/microps/netif.d ./Core/Src/middle/microps/netif.o ./Core/Src/middle/microps/netif.su ./Core/Src/middle/microps/wrap.cyclo ./Core/Src/middle/microps/wrap.d ./Core/Src/middle/microps/wrap.o ./Core/Src/middle/microps/wrap.su

.PHONY: clean-Core-2f-Src-2f-middle-2f-microps

