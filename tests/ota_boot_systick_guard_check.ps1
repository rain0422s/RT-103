$boot = Get-Content -Raw "Bootloader/main.c"

if ($boot -match "HAL_Delay\s*\(" -and
    $boot -notmatch "void\s+SysTick_Handler\s*\(\s*void\s*\)\s*\{(?s).*?HAL_IncTick\s*\(") {
    throw "bootloader uses HAL_Delay but does not provide SysTick_Handler calling HAL_IncTick"
}

Write-Host "OK: bootloader HAL_Delay has a local SysTick tick handler"
