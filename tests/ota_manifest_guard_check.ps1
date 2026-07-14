$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "Ota\ota_manifest.h"
$sourcePath = Join-Path $root "Ota\ota_manifest.c"

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

$header = Read-Text $headerPath
$source = Read-Text $sourcePath

Assert-Contains $header "#define\s+OTA_MANIFEST_MAGIC\s+0x3141544FUL" "manifest magic must be OTA1"
Assert-Contains $header "#define\s+OTA_MANIFEST_VERSION\s+2U" "manifest version must be v2"
Assert-Contains $header "#define\s+OTA_MANIFEST_SIZE\s+68U" "manifest size must be fixed at 68 bytes"
Assert-Contains $header "OTA_MANIFEST_STATE_IDLE\s*=\s*0" "idle state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_RECEIVING\s*=\s*1" "receiving state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_STAGED\s*=\s*2" "staged state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_PENDING\s*=\s*3" "pending state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_APPLIED\s*=\s*4" "applied state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_ERROR\s*=\s*5" "error state missing"
Assert-Contains $header "(?s)typedef\s+struct\s*\{\s*uint32_t\s+magic;\s*uint16_t\s+version;\s*uint16_t\s+header_size;\s*uint32_t\s+state;\s*uint32_t\s+image_size;\s*uint32_t\s+image_crc32;\s*uint32_t\s+received_size;\s*uint32_t\s+target_app_addr;\s*uint32_t\s+target_app_max_size;\s*uint32_t\s+sequence;\s*uint32_t\s+firmware_version;\s*uint32_t\s+min_allowed_version;\s*uint32_t\s+target_slot;\s*uint32_t\s+failure_reason;\s*uint32_t\s+failure_detail;\s*uint32_t\s+checkpoint_size;\s*uint32_t\s+binary_sequence;\s*uint32_t\s+manifest_crc32;\s*\}\s+ota_manifest_t;" "manifest fields must remain complete and ordered"
Assert-Contains $header "_Static_assert\s*\(\s*sizeof\s*\(\s*ota_manifest_t\s*\)\s*==\s*OTA_MANIFEST_SIZE\s*," "manifest struct size must be statically asserted"

Assert-Contains $source "ota_manifest_crc" "manifest crc implementation missing"
Assert-Contains $source "manifest_crc32\s*=\s*0" "manifest crc must zero its own field before calculation"
Assert-Contains $source "ota_boot_state_slot_base" "manifest validation must check slot base"
Assert-Contains $source "OTA_APP_SLOT_SIZE" "manifest validation must check app slot size"
Assert-Contains $source "ota_manifest_state_is_known" "manifest validation must check known state"
Assert-Contains $source "state\s*>=\s*OTA_MANIFEST_STATE_IDLE" "state lower bound missing"
Assert-Contains $source "state\s*<=\s*OTA_MANIFEST_STATE_ERROR" "state upper bound missing"
Assert-Contains $source "ota_manifest_state_is_known\s*\(\s*manifest->state\s*\)" "manifest validation must reject unknown states"

Write-Output "OTA manifest guard OK"
