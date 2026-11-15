################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/adc.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/app.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/ble.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/dac.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/gpio.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/iwdg.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/main.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/stim.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/stm32g4xx_hal_msp.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/stm32g4xx_it.c \
../Application/User/Core/syscalls.c \
../Application/User/Core/sysmem.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/tim.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/usart.c \
C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/usb.c 

OBJS += \
./Application/User/Core/adc.o \
./Application/User/Core/app.o \
./Application/User/Core/ble.o \
./Application/User/Core/dac.o \
./Application/User/Core/gpio.o \
./Application/User/Core/iwdg.o \
./Application/User/Core/main.o \
./Application/User/Core/stim.o \
./Application/User/Core/stm32g4xx_hal_msp.o \
./Application/User/Core/stm32g4xx_it.o \
./Application/User/Core/syscalls.o \
./Application/User/Core/sysmem.o \
./Application/User/Core/tim.o \
./Application/User/Core/usart.o \
./Application/User/Core/usb.o 

C_DEPS += \
./Application/User/Core/adc.d \
./Application/User/Core/app.d \
./Application/User/Core/ble.d \
./Application/User/Core/dac.d \
./Application/User/Core/gpio.d \
./Application/User/Core/iwdg.d \
./Application/User/Core/main.d \
./Application/User/Core/stim.d \
./Application/User/Core/stm32g4xx_hal_msp.d \
./Application/User/Core/stm32g4xx_it.d \
./Application/User/Core/syscalls.d \
./Application/User/Core/sysmem.d \
./Application/User/Core/tim.d \
./Application/User/Core/usart.d \
./Application/User/Core/usb.d 


# Each subdirectory must supply rules for building sources it contributes
Application/User/Core/adc.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/adc.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/app.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/app.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/ble.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/ble.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/dac.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/dac.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/gpio.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/gpio.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/iwdg.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/iwdg.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/main.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/main.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/stim.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/stim.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/stm32g4xx_hal_msp.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/stm32g4xx_hal_msp.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/stm32g4xx_it.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/stm32g4xx_it.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/%.o Application/User/Core/%.su Application/User/Core/%.cyclo: ../Application/User/Core/%.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/tim.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/tim.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/usart.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/usart.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Application/User/Core/usb.o: C:/Users/shrim/Documents/GitHub/Prototype-Generation-1/alphax_tvns_ble/STM32G431kbt6/Core/Src/usb.c Application/User/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../../Core/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc -I../../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-User-2f-Core

clean-Application-2f-User-2f-Core:
	-$(RM) ./Application/User/Core/adc.cyclo ./Application/User/Core/adc.d ./Application/User/Core/adc.o ./Application/User/Core/adc.su ./Application/User/Core/app.cyclo ./Application/User/Core/app.d ./Application/User/Core/app.o ./Application/User/Core/app.su ./Application/User/Core/ble.cyclo ./Application/User/Core/ble.d ./Application/User/Core/ble.o ./Application/User/Core/ble.su ./Application/User/Core/dac.cyclo ./Application/User/Core/dac.d ./Application/User/Core/dac.o ./Application/User/Core/dac.su ./Application/User/Core/gpio.cyclo ./Application/User/Core/gpio.d ./Application/User/Core/gpio.o ./Application/User/Core/gpio.su ./Application/User/Core/iwdg.cyclo ./Application/User/Core/iwdg.d ./Application/User/Core/iwdg.o ./Application/User/Core/iwdg.su ./Application/User/Core/main.cyclo ./Application/User/Core/main.d ./Application/User/Core/main.o ./Application/User/Core/main.su ./Application/User/Core/stim.cyclo ./Application/User/Core/stim.d ./Application/User/Core/stim.o ./Application/User/Core/stim.su ./Application/User/Core/stm32g4xx_hal_msp.cyclo ./Application/User/Core/stm32g4xx_hal_msp.d ./Application/User/Core/stm32g4xx_hal_msp.o ./Application/User/Core/stm32g4xx_hal_msp.su ./Application/User/Core/stm32g4xx_it.cyclo ./Application/User/Core/stm32g4xx_it.d ./Application/User/Core/stm32g4xx_it.o ./Application/User/Core/stm32g4xx_it.su ./Application/User/Core/syscalls.cyclo ./Application/User/Core/syscalls.d ./Application/User/Core/syscalls.o ./Application/User/Core/syscalls.su ./Application/User/Core/sysmem.cyclo ./Application/User/Core/sysmem.d ./Application/User/Core/sysmem.o ./Application/User/Core/sysmem.su ./Application/User/Core/tim.cyclo ./Application/User/Core/tim.d ./Application/User/Core/tim.o ./Application/User/Core/tim.su ./Application/User/Core/usart.cyclo ./Application/User/Core/usart.d ./Application/User/Core/usart.o ./Application/User/Core/usart.su ./Application/User/Core/usb.cyclo ./Application/User/Core/usb.d ./Application/User/Core/usb.o ./Application/User/Core/usb.su

.PHONY: clean-Application-2f-User-2f-Core

