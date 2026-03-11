# RT-103

Environmental Monitoring Station (EMS) based on STM32F103RCT6.

## Core Features

| Module | Description |
|---|---|
| Temperature & Humidity | SHT3x sensor via I2C |
| Motion Detection | LIS2DH12 3-axis accelerometer via SPI |
| Ambient Light Sensing | TEMT6000X01 light sensor via ADC (PA0) |
| Display | LVGL (TFT) and U8G2 (OLED) dual display support |
| Data Storage | W25Qxx SPI Flash (LittleFS) + M24C02 I2C EEPROM |
| Serial Communication | USART1/USART2 with DMA bidirectional forwarding |
| Power Management | Button on/off (short press / long press / double click) with breathing LED indicator |
| RTOS | FreeRTOS multi-task scheduling |

## Hardware

- **MCU**: STM32F103RCT6 (ARM Cortex-M3, 72MHz, 256KB Flash, 48KB RAM)
- **Sensors**: SHT3x (temperature & humidity), LIS2DH12 (accelerometer)
- **Storage**: W25Qxx Flash + M24C02 EEPROM
- **Display**: TFT (LVGL) or OLED (U8G2)
- **LEDs**: Red / Blue status LEDs + PWM breathing LED
- **Button**: Short press, long press, double click detection
- **Programming**: ST-Link (SWD) or Serial (UART)

## Project Structure

```
Core/           STM32CubeMX generated HAL init code
App/            Application modules (sensor, display, storage)
Lib/            Hardware driver libraries (SHT3x, W25Qxx, LIS2DH12, etc.)
Drivers/        STM32 HAL drivers and CMSIS
FreeRTOS/       FreeRTOS kernel source
LVGL/           LVGL v9.3.0 graphics library
```

## Build

Requires [PlatformIO](https://platformio.org/).

```bash
# Install PlatformIO
pip3 install platformio

# Build firmware
pio run

# Upload firmware
pio run -t upload

# Clean build
pio run -t clean
```

Output files are in `.pio/build/genericSTM32F103RC/`:
- `Ems_<version>.hex` — Intel HEX (recommended for flashing)
- `Ems_<version>.bin` — Raw binary
- `Ems_<version>.elf` — ELF (for debugging)
