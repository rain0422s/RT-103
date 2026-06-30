$ErrorActionPreference = "Stop"

$jumpPath = Join-Path $PSScriptRoot "..\Bootloader\boot_jump.c"
$jump = Get-Content -Raw -Path $jumpPath

if ($jump -match "\bHAL_DeInit\s*\(") {
    throw "boot jump must not call HAL_DeInit because it resets GPIOB and drops KEY_OUT/PB10 power hold"
}

if ($jump -notmatch "\bHAL_RCC_DeInit\s*\(") {
    throw "boot jump should still return clocks to a simple reset-like state before app jump"
}

Write-Output "OTA boot jump power-hold guard OK"
