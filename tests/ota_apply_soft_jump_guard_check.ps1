$ota = Get-Content -Raw "App/ota_update.c"

if ($ota -match "HAL_NVIC_SystemReset\s*\(") {
    throw "OTA APPLY must soft-jump to bootloader instead of hard reset"
}

if ($ota -notmatch "ota_jump_to_bootloader\s*\(") {
    throw "OTA APPLY must call a local soft-jump helper"
}

if ($ota -notmatch "OTA_BOOTLOADER_BASE" -or
    $ota -notmatch "OTA_APP_BASE" -or
    $ota -notmatch "OTA_SRAM_BASE" -or
    $ota -notmatch "OTA_SRAM_END") {
    throw "soft jump must validate bootloader vector ranges"
}

if ($ota -notmatch "HAL_GPIO_WritePin\s*\(\s*GPIOB\s*,\s*GPIO_PIN_10\s*,\s*GPIO_PIN_SET\s*\)") {
    throw "soft jump must keep PB10/KEY_OUT high before leaving the app"
}

if ($ota -notmatch "__disable_irq\s*\(\s*\)" -or
    $ota -notmatch "__enable_irq\s*\(\s*\)" -or
    $ota -notmatch "__set_MSP\s*\(" -or
    $ota -notmatch "SCB->VTOR\s*=\s*OTA_BOOTLOADER_BASE") {
    throw "soft jump must switch VTOR/MSP with interrupts controlled"
}

if ($ota -notmatch "SysTick->CTRL\s*=\s*0" -or
    $ota -notmatch "NVIC->ICER" -or
    $ota -notmatch "NVIC->ICPR") {
    throw "soft jump must stop SysTick and clear NVIC enabled/pending IRQs"
}

if ($ota -notmatch "SCB->ICSR\s*=\s*SCB_ICSR_PENDSVCLR_Msk\s*\|\s*SCB_ICSR_PENDSTCLR_Msk") {
    throw "soft jump must clear pending PendSV/SysTick system exceptions"
}

Write-Host "OTA APPLY soft-jump guard OK"
