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
Assert-Contains $header "OTA_MANIFEST_STATE_IDLE\s*=\s*0" "idle state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_RECEIVING\s*=\s*1" "receiving state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_STAGED\s*=\s*2" "staged state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_PENDING\s*=\s*3" "pending state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_APPLIED\s*=\s*4" "applied state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_ERROR\s*=\s*5" "error state missing"
Assert-Contains $header "uint32_t\s+image_size" "image size field missing"
Assert-Contains $header "uint32_t\s+image_crc32" "image crc field missing"
Assert-Contains $header "uint32_t\s+received_size" "received size field missing"
Assert-Contains $header "uint32_t\s+target_app_addr" "target app addr field missing"
Assert-Contains $header "uint32_t\s+target_app_max_size" "target max size field missing"
Assert-Contains $header "uint32_t\s+manifest_crc32" "manifest crc field missing"

Assert-Contains $source "ota_manifest_crc" "manifest crc implementation missing"
Assert-Contains $source "manifest_crc32\s*=\s*0" "manifest crc must zero its own field before calculation"
Assert-Contains $source "OTA_APP_BASE" "manifest validation must check app base"
Assert-Contains $source "OTA_APP_SIZE" "manifest validation must check app size"

Write-Output "OTA manifest guard OK"
