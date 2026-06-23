$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$displayPath = Join-Path $root "App\display.c"
$display = Get-Content -Raw -LiteralPath $displayPath

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Contains $display '#define\s+UI_CLOCK_DIGIT_ONE_GAP_EXTRA\s+([0-9]+)' `
    "clock layout must define extra spacing for digit 1 pairs"
Assert-Contains $display 'static\s+short\s+ui_7seg_pair_gap\s*\(\s*uint8_t\s+left_digit,\s*uint8_t\s+right_digit\s*\)' `
    "clock layout must use a pair gap helper for digit-specific visual spacing"
Assert-Contains $display 'left_digit\s*==\s*1u\s*\|\|\s*right_digit\s*==\s*1u' `
    "digit 1 next to another digit must receive extra visual spacing"
Assert-Contains $display 'rel_x\[1\]\s*=\s*\(short\)\(rel_x\[0\]\s*\+\s*right\[0\]\s*\+\s*ui_7seg_pair_gap\s*\(\s*digits\[0\],\s*digits\[1\]\s*\)\s*-\s*left\[1\]\)' `
    "hour digit spacing must use the digit-specific pair gap"
Assert-Contains $display 'rel_x\[3\]\s*=\s*\(short\)\(rel_x\[2\]\s*\+\s*right\[2\]\s*\+\s*ui_7seg_pair_gap\s*\(\s*digits\[2\],\s*digits\[3\]\s*\)\s*-\s*left\[3\]\)' `
    "minute digit spacing must use the digit-specific pair gap"
Assert-Contains $display 'colon_x\s*=\s*\(short\)\(rel_x\[1\]\s*\+\s*right\[1\]\s*\+\s*UI_CLOCK_COLON_GAP\)' `
    "colon spacing must stay independent from digit 1 pair spacing"

$extraMatch = [regex]::Match($display, '#define\s+UI_CLOCK_DIGIT_ONE_GAP_EXTRA\s+([0-9]+)')
if ($extraMatch.Success) {
    $extra = [int]$extraMatch.Groups[1].Value
    if ($extra -lt 1 -or $extra -gt 3) {
        throw "digit 1 extra spacing must stay subtle and visual, expected 1..3 pixels"
    }
}

Write-Output "Clock digit spacing guard OK"
