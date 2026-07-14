$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "Ota\ota_manifest.h"
$sourcePath = Join-Path $root "Ota\ota_manifest.c"
$bootOtaPath = Join-Path $root "Bootloader\boot_ota.c"
$appOtaPath = Join-Path $root "App\ota_update.c"

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
$bootOta = Read-Text $bootOtaPath
$appOta = Read-Text $appOtaPath

Assert-Contains $header "#define\s+OTA_MANIFEST_VERSION\s+2U" "manifest version must be v2"
Assert-Contains $header "#define\s+OTA_MANIFEST_SIZE\s+68U" "manifest v2 must be 68 bytes"
Assert-Contains $header "typedef\s+enum\s*\{(?s).*OTA_FAIL_NONE\s*=\s*0" "OTA failure reason enum must include NONE"
Assert-Contains $header "OTA_FAIL_HEADER_INVALID" "OTA failure reason must include HEADER_INVALID"
Assert-Contains $header "OTA_FAIL_IMAGE_CRC_MISMATCH" "OTA failure reason must include IMAGE_CRC_MISMATCH"
Assert-Contains $header "OTA_FAIL_FLASH_ERASE_FAILED" "OTA failure reason must include FLASH_ERASE_FAILED"
Assert-Contains $header "OTA_FAIL_FLASH_PROGRAM_FAILED" "OTA failure reason must include FLASH_PROGRAM_FAILED"
Assert-Contains $header "OTA_FAIL_APP_CRC_MISMATCH" "OTA failure reason must include APP_CRC_MISMATCH"
Assert-Contains $header "OTA_FAIL_VERSION_ROLLBACK" "OTA failure reason must include VERSION_ROLLBACK"
Assert-Contains $header "OTA_FAIL_BOOT_CONFIRM_TIMEOUT" "OTA failure reason must include BOOT_CONFIRM_TIMEOUT"
Assert-Contains $header "uint32_t\s+firmware_version;" "manifest must store firmware version"
Assert-Contains $header "uint32_t\s+min_allowed_version;" "manifest must store minimum allowed version"
Assert-Contains $header "uint32_t\s+target_slot;" "manifest must store target slot"
Assert-Contains $header "uint32_t\s+failure_reason;" "manifest must store failure reason"
Assert-Contains $header "uint32_t\s+failure_detail;" "manifest must store failure detail"
Assert-Contains $header "uint32_t\s+checkpoint_size;" "manifest must store resume checkpoint size"
Assert-Contains $header "uint32_t\s+binary_sequence;" "manifest must store binary packet sequence"

Assert-Contains $source "firmware_version\s*=\s*OTA_FW_VERSION" "manifest init must default firmware version"
Assert-Contains $source "checkpoint_size\s*=\s*OTA_RESUME_CHECKPOINT_SIZE" "manifest init must set checkpoint size"
Assert-Contains $source "ota_manifest_set_failure" "manifest must expose failure setter"
Assert-Contains $source "manifest->firmware_version" "manifest validation must include firmware version"
Assert-Contains $source "manifest->target_slot" "manifest validation must include target slot"

Assert-Contains $bootOta "ota_manifest_set_failure" "bootloader OTA apply must persist failure reason codes"
Assert-Contains $bootOta "OTA_FAIL_HEADER_INVALID" "bootloader must mark header validation failures"
Assert-Contains $bootOta "OTA_FAIL_IMAGE_CRC_MISMATCH" "bootloader must mark staged image CRC failures"
Assert-Contains $bootOta "OTA_FAIL_FLASH_ERASE_FAILED" "bootloader must mark erase failures"
Assert-Contains $bootOta "OTA_FAIL_FLASH_PROGRAM_FAILED" "bootloader must mark program failures"
Assert-Contains $bootOta "OTA_FAIL_APP_CRC_MISMATCH" "bootloader must mark programmed app CRC failures"
Assert-Contains $bootOta "OTA_FAIL_VERSION_ROLLBACK" "bootloader must reject rollback versions"

Assert-Contains $appOta "active=%lu fail=%lu detail=%lu" "OTA STATUS must include active slot plus manifest failure reason and detail"

Write-Output "OTA manifest v2 guard OK"
