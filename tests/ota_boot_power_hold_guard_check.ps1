$ErrorActionPreference = "Stop"

$bootPath = Join-Path $PSScriptRoot "..\Bootloader\main.c"
$boot = Get-Content -Raw -Path $bootPath

if ($boot -notmatch "BOOT_POWER_HOLD_GPIO_PORT\s+GPIOB" -or
    $boot -notmatch "BOOT_POWER_HOLD_GPIO_PIN\s+GPIO_PIN_10") {
    throw "bootloader must define PB10 as the board power-hold output"
}
if ($boot -notmatch "BOOT_POWER_KEY_GPIO_PORT\s+GPIOB" -or
    $boot -notmatch "BOOT_POWER_KEY_GPIO_PIN\s+GPIO_PIN_11") {
    throw "bootloader must define PB11 as the manual power-key input"
}

$gpioCall = $boot.IndexOf("boot_gpio_init();")
$clockCall = $boot.IndexOf("BootClock_Config();")
$flashReset = $boot.IndexOf("w25qxx_reset();")

if ($boot -match "\bMX_GPIO_Init\s*\(") {
    throw "bootloader must not call shared MX_GPIO_Init because it drives PB10 low before power hold"
}
if ($boot -match "\bMX_SPI1_Init\s*\(") {
    throw "bootloader must not call shared MX_SPI1_Init because it blocks in Error_Handler on SPI init failure"
}

if ($gpioCall -lt 0) {
    throw "bootloader must run its own minimal GPIO init"
}
if ($boot -notmatch "\bboot_spi1_init\s*\(") {
    throw "bootloader must use a non-blocking local SPI1 init"
}
if ($boot -notmatch "\bboot_power_key_pressed\s*\(" -or
    $boot -notmatch "\bs_boot_power_key_latched\b" -or
    $boot -notmatch "\bboot_power_key_bypass_requested\s*\(" -or
    $boot -notmatch "(?s)s_boot_power_key_latched\s*=\s*boot_power_key_pressed\s*\(\s*\).*?w25qxx_reset\s*\(" -or
    $boot -notmatch "(?s)if\s*\(\s*!\s*boot_power_key_bypass_requested\s*\(\s*\)\s*\)\s*\{.*?boot_ota_apply_if_pending\s*\(") {
    throw "bootloader must bypass pending OTA while the manual power key is held"
}
if ($clockCall -lt 0 -or $flashReset -lt 0 -or
    $gpioCall -gt $clockCall -or $gpioCall -gt $flashReset) {
    throw "bootloader GPIO init must assert power hold before clock, flash, and OTA work"
}

if ($boot -notmatch "HAL_GPIO_WritePin\s*\(\s*BOOT_POWER_HOLD_GPIO_PORT\s*,\s*BOOT_POWER_HOLD_GPIO_PIN\s*,\s*GPIO_PIN_SET\s*\)") {
    throw "bootloader power hold must drive PB10 high"
}

Write-Output "OTA boot power-hold guard OK"
