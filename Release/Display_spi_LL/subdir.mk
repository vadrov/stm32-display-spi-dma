################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (10.3-2021.10)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Display_spi_LL/display.c \
../Display_spi_LL/fonts.c \
../Display_spi_LL/ili9341.c \
../Display_spi_LL/st7789.c 

OBJS += \
./Display_spi_LL/display.o \
./Display_spi_LL/fonts.o \
./Display_spi_LL/ili9341.o \
./Display_spi_LL/st7789.o 

C_DEPS += \
./Display_spi_LL/display.d \
./Display_spi_LL/fonts.d \
./Display_spi_LL/ili9341.d \
./Display_spi_LL/st7789.d 


# Each subdirectory must supply rules for building sources it contributes
Display_spi_LL/%.o Display_spi_LL/%.su: ../Display_spi_LL/%.c Display_spi_LL/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -DSTM32F401xC -DUSE_FULL_LL_DRIVER -DHSE_VALUE=25000000 -DHSE_STARTUP_TIMEOUT=100 -DLSE_STARTUP_TIMEOUT=5000 -DLSE_VALUE=32768 -DEXTERNAL_CLOCK_VALUE=12288000 -DHSI_VALUE=16000000 -DLSI_VALUE=32000 -DVDD_VALUE=3300 -DPREFETCH_ENABLE=1 -DINSTRUCTION_CACHE_ENABLE=1 -DDATA_CACHE_ENABLE=1 -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"F:/stm32-display-spi-dma-main/Display_spi_LL" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Display_spi_LL

clean-Display_spi_LL:
	-$(RM) ./Display_spi_LL/display.d ./Display_spi_LL/display.o ./Display_spi_LL/display.su ./Display_spi_LL/fonts.d ./Display_spi_LL/fonts.o ./Display_spi_LL/fonts.su ./Display_spi_LL/ili9341.d ./Display_spi_LL/ili9341.o ./Display_spi_LL/ili9341.su ./Display_spi_LL/st7789.d ./Display_spi_LL/st7789.o ./Display_spi_LL/st7789.su

.PHONY: clean-Display_spi_LL

