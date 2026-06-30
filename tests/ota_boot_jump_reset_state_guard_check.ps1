$ErrorActionPreference = "Stop"

$jumpPath = Join-Path $PSScriptRoot "..\Bootloader\boot_jump.c"
$jump = Get-Content -Raw -Path $jumpPath

if ($jump -notmatch "NVIC->ICER" -or $jump -notmatch "NVIC->ICPR") {
    throw "boot jump must clear enabled and pending NVIC IRQ state before entering the app"
}

if ($jump -notmatch "SCB->ICSR\s*=\s*SCB_ICSR_PENDSVCLR_Msk\s*\|\s*SCB_ICSR_PENDSTCLR_Msk") {
    throw "boot jump must clear pending PendSV/SysTick system exceptions before entering the app"
}

if ($jump -notmatch "__set_BASEPRI\s*\(\s*0U\s*\)" -or
    $jump -notmatch "__set_FAULTMASK\s*\(\s*0U\s*\)" -or
    $jump -notmatch "__set_PSP\s*\(\s*0U\s*\)" -or
    $jump -notmatch "__set_CONTROL\s*\(\s*0U\s*\)") {
    throw "boot jump must restore core state to an MSP/unmasked app entry"
}

if ($jump -notmatch "__DSB\s*\(\s*\)" -or $jump -notmatch "__ISB\s*\(\s*\)") {
    throw "boot jump must issue barriers after changing VTOR/MSP/core state"
}

if ($jump -notmatch "__HAL_FLASH_PREFETCH_BUFFER_DISABLE\s*\(" -or
    $jump -notmatch "__HAL_FLASH_PREFETCH_BUFFER_ENABLE\s*\(") {
    throw "boot jump must refresh the STM32F1 flash prefetch buffer after OTA flash programming"
}

if ($jump -notmatch "DMA1_Channel1->CCR\s*=\s*0U" -or
    $jump -notmatch "DMA1->IFCR\s*=\s*0x0FFFFFFFUL") {
    throw "boot jump must disable DMA channels and clear stale DMA interrupt flags"
}

if ($jump -notmatch "RCC->APB1RSTR" -or
    $jump -notmatch "RCC->APB2RSTR" -or
    $jump -notmatch "boot_reset_app_peripherals\s*\(") {
    throw "boot jump must reset app runtime peripherals before entering the app"
}

Write-Output "OTA boot jump reset-state guard OK"
