$ErrorActionPreference = "Stop"

$gpioPath = Join-Path $PSScriptRoot "..\Core\Src\gpio.c"
$gpio = Get-Content -Raw -Path $gpioPath

if ($gpio -match "HAL_GPIO_WritePin\s*\(\s*GPIOB\s*,[^;]*GPIO_PIN_10[^;]*,\s*GPIO_PIN_RESET\s*\)") {
    throw "app GPIO init must not drive KEY_OUT/PB10 low"
}

if ($gpio -notmatch "HAL_GPIO_WritePin\s*\(\s*GPIOB\s*,\s*GPIO_PIN_10\s*,\s*GPIO_PIN_SET\s*\)") {
    throw "app GPIO init must drive KEY_OUT/PB10 high"
}

if ($gpio -notmatch "(?s)HAL_GPIO_WritePin\s*\(\s*GPIOB\s*,\s*GPIO_PIN_10\s*,\s*GPIO_PIN_SET\s*\).*?GPIO_InitStruct\.Pin\s*=[^;]*GPIO_PIN_10[^;]*;.*?HAL_GPIO_Init\s*\(\s*GPIOB\s*,\s*&GPIO_InitStruct\s*\)") {
    throw "app GPIO init must set PB10 high before configuring it as output"
}

Write-Output "App power-hold GPIO guard OK"
