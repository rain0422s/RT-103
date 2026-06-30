$ErrorActionPreference = "Stop"

$powerPath = Join-Path $PSScriptRoot "..\App\power_key.c"
$power = Get-Content -Raw -Path $powerPath

if ($power -notmatch "button_scan\s*\(\s*true") {
    throw "power shutdown must require PA1 button_scan confirmation after PB11 long press"
}

if ($power -notmatch "PowerDown") {
    throw "power shutdown path must drive the board power-enable pin low"
}

$stackMatch = [regex]::Match($power, 'xTaskCreate\s*\(\s*power_key_task\s*,\s*"power_key"\s*,\s*(\d+)')
if (-not $stackMatch.Success) {
    throw "power_key task stack size was not found"
}

$stackWords = [int]$stackMatch.Groups[1].Value
if ($stackWords -lt 768) {
    throw "power_key stack must be at least 768 words because shutdown calls storage_prepare_shutdown"
}

Write-Output "Power shutdown guard OK"
