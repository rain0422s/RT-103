$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$displayPath = Join-Path $root "App\display.c"
$oledPath = Join-Path $root "Lib\oled\oled.c"
$oledHeaderPath = Join-Path $root "Lib\oled\oled.h"

$display = Get-Content -Raw -LiteralPath $displayPath
$oled = Get-Content -Raw -LiteralPath $oledPath
$oledHeader = Get-Content -Raw -LiteralPath $oledHeaderPath

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

function Assert-NotContains($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) {
        throw $Message
    }
}

Assert-Contains $oledHeader 'oled_boot_splash_animate\s*\(\s*u8g2_t\s*\*\s*u8g2,\s*uint16_t\s+duration_ms\s*\)' `
    "boot splash animation API must be exposed"
Assert-Contains $oled 'static\s+void\s+oled_boot_splash_draw_frame\s*\(' `
    "boot splash must draw multiple animation frames"
Assert-Contains $oled 'u8g2_DrawArc\s*\(' `
    "boot splash must use arc/spinner-style motion"
Assert-Contains $oled 'u8g2_DrawHLine\s*\(' `
    "boot splash must include a subtle loading scan/progress line"
Assert-Contains $display 'oled_boot_splash_animate\s*\(\s*pu8g2,\s*UI_BOOT_IMAGE_MS\s*\)' `
    "initial boot screen must animate without adding a separate delay"
Assert-Contains $display 'oled_boot_splash_show\s*\(&u8g2\)' `
    "storage wait path must keep refreshing the boot splash frame"

Assert-Contains $display 'static\s+void\s+ui_draw_shutdown_progress_ring\s*\(' `
    "shutdown UI must draw a circular hold progress indicator"
Assert-Contains $display 'u8g2_DrawArc\s*\(\s*pu8g2' `
    "shutdown ring must use u8g2 arc drawing"
Assert-Contains $display 's_shutdown_prompt_progress_pct' `
    "shutdown ring must be driven by the existing power-key progress percentage"
Assert-Contains $display 'static\s+void\s+ui_draw_shutdown_power_icon\s*\(' `
    "shutdown UI must use a minimal center power icon"
Assert-Contains $display '"HOLD"' `
    "shutdown UI must use a short HOLD label before confirmation"
Assert-Contains $display '"OFF"' `
    "shutdown UI must use a short OFF label after confirmation"
Assert-NotContains $display 'Hold to power off' `
    "shutdown UI must not use long hold instruction text"
Assert-NotContains $display 'Release cancels' `
    "shutdown UI must not use long cancel instruction text"
Assert-NotContains $display 'Powering off\.\.\.' `
    "shutdown UI must not use long power-off status text"

Write-Output "Boot/shutdown animation guard OK"
