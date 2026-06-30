$ErrorActionPreference = "Stop"

$powerPath = Join-Path $PSScriptRoot "..\App\power_key.c"
$power = Get-Content -Raw -Path $powerPath

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Contains $power "\bpower_key_confirm_shutdown\s*\(" "power-key shutdown must ask the other key for confirmation"
Assert-Contains $power "button_scan\s*\(\s*true\s*," "shutdown confirmation must use the timed other-key scan"
Assert-Contains $power "SHORT_PRESS_STATE" "shutdown confirmation must accept an explicit short press on the other key"
Assert-Contains $power "(?s)if\s*\(\s*power_key_confirm_shutdown\s*\(\s*\)\s*\)\s*\{.*?PowerDown" "PowerDown must only run after the other key confirms shutdown"
Assert-Contains $power "(?s)else\s*\{.*?display_show_shutdown_prompt\s*\(\s*false\s*,\s*false\s*,\s*0\s*\).*?resume_periodic_tasks\s*\(\s*\).*?s_shutdown_active\s*=\s*false" "shutdown confirmation timeout/cancel must hide prompt, resume tasks, and clear active state"

Write-Output "Power-key shutdown confirm guard OK"
