$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$profilePath = Join-Path $root "App\battery_profile.h"
$sensorPath = Join-Path $root "App\sensor.c"
$displayPath = Join-Path $root "App\display.c"
$uartPath = Join-Path $root "App\uart_forward.c"
$docPath = Join-Path $root "docs\rt103_battery_702035.md"
$readmePath = Join-Path $root "README.md"

function Read-Text($Path) {
    if (-not (Test-Path $Path)) {
        throw "Missing required file: $Path"
    }
    return Get-Content -Raw -Path $Path
}

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

$profile = Read-Text $profilePath
$sensor = Read-Text $sensorPath
$display = Read-Text $displayPath
$uart = Read-Text $uartPath
$doc = Read-Text $docPath
$readme = Read-Text $readmePath

Assert-Contains $profile 'RT103_BATTERY_MODEL_NAME\s+"702035"' "battery model must be 702035"
Assert-Contains $profile 'RT103_BATTERY_CAPACITY_MAH\s+500U' "battery capacity must be 500mAh"
Assert-Contains $profile 'RT103_BATTERY_FULL_MV\s+4200U' "full voltage must be 4200mV"
Assert-Contains $profile 'RT103_BATTERY_CHARGE_LIMIT_MAX_MV\s+4230U' "charge limit tolerance max must be 4230mV"
Assert-Contains $profile 'RT103_BATTERY_MAX_CONTINUOUS_CHARGE_MA\s+250U' "charge current must be capped at 250mA"
Assert-Contains $profile 'RT103_BATTERY_MAX_CONTINUOUS_DISCHARGE_MA\s+250U' "discharge current must be capped at 250mA"
Assert-Contains $profile 'RT103_BATTERY_CHARGE_TEMP_MAX_C\s+45' "charge temperature max must be 45C"
Assert-Contains $profile 'RT103_BATTERY_PROTECT_UNDERVOLT_TYP_MV\s+3000U' "protection undervoltage point must be 3000mV typical"
Assert-Contains $profile 'RT103_BATTERY_UI_EMPTY_MV\s+3300U' "UI empty voltage must remain above protection cutoff"

Assert-Contains $sensor '#include\s+"battery_profile\.h"' "sensor code must include the battery profile"
Assert-Contains $sensor 'BATTERY_CAL_MIN_MV\s+RT103_BATTERY_PROTECT_UNDERVOLT_TYP_MV' "BATCAL min must use the protected-cell profile"
Assert-Contains $sensor 'BATTERY_CAL_MAX_MV\s+RT103_BATTERY_CHARGE_LIMIT_MAX_MV' "BATCAL max must use the protected-cell profile"
Assert-Contains $sensor 'RT103_BATTERY_UI_EMPTY_MV,\s*0U' "OCV table must derive empty point from profile"
Assert-Contains $sensor 'RT103_BATTERY_FULL_MV,\s*100U' "OCV table must derive full point from profile"

Assert-Contains $display '#include\s+"battery_profile\.h"' "display code must include the battery profile"
Assert-Contains $display 'UI_BAT_CHART_V_MIN_MV\s+RT103_BATTERY_CHART_MIN_MV' "chart minimum must use the battery profile"
Assert-Contains $display 'UI_BAT_CHART_V_MAX_MV\s+RT103_BATTERY_CHART_MAX_MV' "chart maximum must use the battery profile"

Assert-Contains $uart '#include\s+"battery_profile\.h"' "UART code must include the battery profile"
Assert-Contains $uart 'whole\s+<\s+RT103_BATTERY_PROTECT_UNDERVOLT_TYP_MV' "BATCAL lower range must reject below protection cutoff"
Assert-Contains $uart 'whole\s+>\s+RT103_BATTERY_CHARGE_LIMIT_MAX_MV' "BATCAL upper range must reject above safe charge limit"

Assert-Contains $doc '702035' "battery documentation must name the fitted cell"
Assert-Contains $doc '500mAh' "battery documentation must include capacity"
Assert-Contains $doc '250mA' "battery documentation must include current limit"
Assert-Contains $readme 'App/battery_profile\.h' "README must point to battery profile"

if ($sensor -match 'BATTERY_CAPACITY_MAH\s+2000U|BATTERY_CHARGE_CURRENT_MIN_MA\s+1000U|BATTERY_CHARGE_CURRENT_MAX_MA\s+2000U') {
    throw "stale high-capacity/high-current battery constants must not remain in sensor.c"
}

Write-Output "Battery profile guard OK"
