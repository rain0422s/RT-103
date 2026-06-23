$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$displayPath = Join-Path $root "App\display.c"
$storagePath = Join-Path $root "App\storage.c"
$registryPath = Join-Path $root "App\ui_menu_registry.c"

$display = Get-Content -Raw -LiteralPath $displayPath
$storage = Get-Content -Raw -LiteralPath $storagePath
$registry = Get-Content -Raw -LiteralPath $registryPath

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

Assert-Contains $registry 'reg_fn\("Pose Calib",\s*display_action_pose_calib,\s*UI_MENU_MODE_ALL\)' `
    "merged calibration menu must keep the Pose Calib entry"
Assert-NotContains $registry 'Accel Calib' `
    "Accel Calib must not be registered as a separate top-level menu entry"
Assert-NotContains $storage 'reg_fn\("Accel Calib"' `
    "storage menu registration must not add a separate Accel Calib entry"

$poseStart = $display.IndexOf('void display_action_pose_calib')
$poseEnd = $display.IndexOf('static bool ui_menu_register', $poseStart)
if ($poseStart -lt 0 -or $poseEnd -lt 0 -or $poseEnd -le $poseStart) {
    throw "display_action_pose_calib not found"
}
$poseAction = $display.Substring($poseStart, $poseEnd - $poseStart)

Assert-Contains $poseAction 'storage_run_lis2dh12_calibration_save\s*\(\s*\)' `
    "Pose Calib action must also run LIS2DH12 accel calibration"
Assert-Contains $poseAction 'sensor_request_active\s*\(' `
    "Pose Calib action must request fresh high-rate attitude samples"
Assert-Contains $poseAction 'sensor_pose_capture_config\s*\(&s_ui_config\)' `
    "Pose Calib action must save the flat pose after accel calibration"
Assert-Contains $poseAction 'Accel Failed' `
    "merged calibration action must report accel calibration failure distinctly"
Assert-Contains $poseAction 'Saved Both' `
    "merged calibration action must report that both accel and pose calibration were saved"

$accelCall = $poseAction.IndexOf('storage_run_lis2dh12_calibration_save')
$poseCall = $poseAction.IndexOf('sensor_pose_capture_config')
if ($accelCall -lt 0 -or $poseCall -lt 0 -or $accelCall -gt $poseCall) {
    throw "accel calibration must run before pose capture"
}

Write-Output "Merged calibration menu guard OK"
