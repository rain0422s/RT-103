# RT-103

Environmental Monitoring Station (EMS) based on STM32F103RCT6.

## Core Features

| Module | Description |
|---|---|
| Temperature & Humidity | SHT3x sensor via I2C |
| Motion Detection | LIS2DH12 3-axis accelerometer via SPI |
| Ambient Light Sensing | TEMT6000X01 light sensor via ADC (PA0) |
| Display | U8G2 OLED display |
| Data Storage | W25Qxx SPI Flash (LittleFS) + M24C02 I2C EEPROM |
| Serial Communication | USART1/USART2 with DMA bidirectional forwarding |
| Power Management | Button on/off (short press / long press / double click) with breathing LED indicator |
| RTOS | FreeRTOS multi-task scheduling |

## Hardware

- **MCU**: STM32F103RCT6 (ARM Cortex-M3, 72MHz, 256KB Flash, 48KB RAM)
- **Sensors**: SHT3x (temperature & humidity), LIS2DH12 (accelerometer)
- **Storage**: W25Qxx Flash + M24C02 EEPROM
- **Display**: OLED (U8G2)
- **LEDs**: Red / Blue status LEDs + PWM breathing LED
- **Button**: Short press, long press, double click detection
- **Battery**: 702035 protected 1S LiPo, 3.7V / 500mAh
- **Programming**: ST-Link (SWD) or Serial (UART)

## Battery Profile

RT-103 is configured for a 702035 protected 1S LiPo pack. The firmware profile
lives in `App/battery_profile.h`; the project note is
`docs/rt103_battery_702035.md`.

Key limits:
- Full voltage: `4200mV`, charger tolerance max `4230mV`
- Max continuous charge current: `250mA`
- Max continuous discharge current: `250mA`
- UI empty point: `3300mV`
- Reserved low warning / soft shutdown points: `3400mV` / `3200mV`

## Project Structure

```
Core/           STM32CubeMX generated HAL init code
App/            Application modules (sensor, display, storage)
Lib/            Hardware driver libraries (SHT3x, W25Qxx, LIS2DH12, etc.)
Drivers/        STM32 HAL drivers and CMSIS
FreeRTOS/       FreeRTOS kernel source
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

## OTA Builds

The two-stage OTA build uses separate PlatformIO environments:

```bash
pio run -e bootloader
pio run -e app
```

Flash the bootloader at `0x08000000`. The app image is linked for
`0x08008000`. The USART1 text OTA protocol accepts `OTA BEGIN`, `OTA DATA`,
`OTA END`, `OTA APPLY`, `OTA ABORT`, and `OTA STATUS?`. OTA images are staged
in W25Q16 before the bootloader installs them into the internal Flash app
partition.
