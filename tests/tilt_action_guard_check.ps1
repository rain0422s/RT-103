$ErrorActionPreference = "Stop"

$sensorPath = Join-Path $PSScriptRoot "..\App\sensor.c"
$displayPath = Join-Path $PSScriptRoot "..\App\display.c"
$sensor = Get-Content -Raw -Path $sensorPath
$display = Get-Content -Raw -Path $displayPath

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

Assert-Contains $sensor "TILT_ACTION_DELTA_DEG" `
    "tilt action must use relative roll delta, not only absolute held angle"
Assert-Contains $sensor "TILT_STATE_MOVING_RIGHT" `
    "tilt action must track a moving-right state before latching held"
Assert-Contains $sensor "TILT_STATE_MOVING_LEFT" `
    "tilt action must track a moving-left state before latching held"
Assert-Contains $sensor "physical_tilt > TILT_ACTION_TRIGGER_DEG" `
    "right tilt must require menu tilt axis to be on the right-down side"
Assert-Contains $sensor "physical_tilt < -TILT_ACTION_TRIGGER_DEG" `
    "left tilt must require menu tilt axis to be on the left-down side"
Assert-Contains $sensor "sensor_request_active" `
    "menu interaction must be able to request high-rate sensor sampling"
Assert-Contains $sensor "TILT_STABLE_BASELINE_MS" `
    "tilt baseline must update only after a stable hold, not immediately on low rate"
Assert-Contains $sensor "physical_tilt\s*=\s*angle_delta_deg\s*\(\s*pitch_deg\s*,\s*s_pose_pitch_zero_deg\s*\)" `
    "menu tilt axis must use pitch/X, separate from screen rotation axis"
Assert-Contains $sensor "physical_rate\s*=\s*pitch_rate_dps\s*\*\s*\(float\)s_pose_roll_sign" `
    "menu tilt rate sign must match physical tilt sign"
Assert-Contains $sensor "physical_screen\s*=\s*angle_delta_deg\s*\(\s*roll_deg\s*,\s*s_pose_roll_zero_deg\s*\)" `
    "screen/up-down axis must use roll/Y only as a gate"
Assert-Contains $sensor "angle_delta_deg\s*\(\s*physical_tilt\s*,\s*s_tilt_ref_deg\s*\)" `
    "right tilt delta must use wrapped angle difference"
Assert-Contains $sensor "angle_delta_deg\s*\(\s*s_tilt_ref_deg\s*,\s*physical_tilt\s*\)" `
    "left tilt delta must use wrapped angle difference"
Assert-Contains $sensor "TILT_ACTION_MAX_PITCH_DEG" `
    "tilt action must reject non-flat/upright posture"
Assert-Contains $sensor "fabsf\s*\(\s*physical_screen\s*\)\s*>\s*TILT_ACTION_MAX_PITCH_DEG" `
    "up/down screen-rotation axis must not trigger left/right menu events"
Assert-Contains $sensor "TILT_ACTION_MAX_DYNACC_MG" `
    "tilt action must reject whole-device lift or shake acceleration"
Assert-Contains $sensor "dyn_acc_mg\s*>\s*TILT_ACTION_MAX_DYNACC_MG" `
    "linear lift acceleration must not trigger left/right menu events"
Assert-NotContains $sensor "s_tilt_state\s*=\s*TILT_STATE_HELD_RIGHT;\s*if\s*\(roll_rate_dps" `
    "right tilt must not enter HELD before confirming right-down motion"
Assert-NotContains $sensor "s_tilt_state\s*=\s*TILT_STATE_HELD_LEFT;\s*if\s*\(roll_rate_dps" `
    "left tilt must not enter HELD before confirming left-down motion"
Assert-NotContains $sensor "if\s*\(abs_rate\s*<=\s*TILT_ACTION_REARM_RATE_DPS\)\s*\{\s*sensor_tilt_action_arm_at\(physical_tilt\);" `
    "slow motion must not immediately become the new baseline"

$triggerMatch = [regex]::Match($sensor, '#define\s+TILT_ACTION_TRIGGER_DEG\s+([0-9.]+)f')
$deltaMatch = [regex]::Match($sensor, '#define\s+TILT_ACTION_DELTA_DEG\s+([0-9.]+)f')
$rateMatch = [regex]::Match($sensor, '#define\s+TILT_ACTION_RATE_DPS\s+([0-9.]+)f')
$alphaMatch = [regex]::Match($sensor, '#define\s+ATT_LPF_ALPHA\s+([0-9.]+)f')
$sampleMatch = [regex]::Match($sensor, '#define\s+SENSOR_ACTIVE_SAMPLE_MS\s+(\d+)U')
$watermarkMatch = [regex]::Match($sensor, '#define\s+SENSOR_FIFO_WTM_ACTIVE\s+(\d+)U')
$dynMatch = [regex]::Match($sensor, '#define\s+TILT_ACTION_MAX_DYNACC_MG\s+([0-9.]+)f')
if (-not $triggerMatch.Success -or -not $deltaMatch.Success -or -not $rateMatch.Success -or
    -not $alphaMatch.Success -or -not $sampleMatch.Success -or -not $watermarkMatch.Success -or
    -not $dynMatch.Success) {
    throw "tilt action thresholds were not found"
}

if ([double]$triggerMatch.Groups[1].Value -gt 5.0) {
    throw "tilt trigger angle must stay sensitive enough for small actions"
}

if ([double]$deltaMatch.Groups[1].Value -gt 2.5) {
    throw "tilt delta angle must stay sensitive enough for small actions"
}

if ([double]$rateMatch.Groups[1].Value -gt 3.0) {
    throw "tilt rate threshold must stay sensitive enough for small actions"
}

if ([double]$alphaMatch.Groups[1].Value -lt 0.20) {
    throw "attitude filter must respond quickly enough for small menu gestures"
}

if ([int]$sampleMatch.Groups[1].Value -gt 25) {
    throw "active sensor sampling interval must be fast enough for menu gestures"
}

if ([int]$watermarkMatch.Groups[1].Value -gt 2) {
    throw "active FIFO watermark must be low enough for small menu gestures"
}

if ([double]$dynMatch.Groups[1].Value -gt 180.0) {
    throw "linear acceleration rejection must be strict enough to ignore upward lift"
}

Assert-Contains $display "event\s*==\s*SENSOR_TILT_EVENT_RIGHT\)\s*\r?\n\s*delta\s*=\s*1" `
    "right tilt must move list selection by +1"
Assert-Contains $display "event\s*==\s*SENSOR_TILT_EVENT_LEFT\)\s*\r?\n\s*delta\s*=\s*-1" `
    "left tilt must move list selection by -1"
Assert-Contains $display "sensor_request_active\s*\(\s*UI_SENSOR_ACTIVE_HOLD_MS\s*\);\s*\r?\n\s*active\s*=\s*true" `
    "menu mode must keep fast UI polling so tilt action and list switch happen together"

Write-Output "Tilt action guard OK"
