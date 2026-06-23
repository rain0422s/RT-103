$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "Ota\ota_store.h"
$sourcePath = Join-Path $root "Ota\ota_store.c"

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

Assert-Contains $header "ota_store_read_manifest" "read manifest prototype missing"
Assert-Contains $header "ota_store_write_manifest" "write manifest prototype missing"
Assert-Contains $header "ota_store_clear_manifest" "clear manifest prototype missing"
Assert-Contains $header "ota_store_erase_image_slot" "erase image slot prototype missing"
Assert-Contains $header "ota_store_write_image" "write image prototype missing"
Assert-Contains $header "ota_store_read_image" "read image prototype missing"
Assert-Contains $source "ota_manifest_finalize" "manifest writes must refresh the manifest CRC"
Assert-Contains $source "OTA_MANIFEST_ADDR" "manifest address constant must be used"
Assert-Contains $source "OTA_IMAGE_ADDR" "image address constant must be used"
Assert-Contains $source "OTA_IMAGE_SLOT_SIZE" "image slot size must be checked"
Assert-Contains $source "OTA_W25Q_SECTOR_SIZE" "image erase must be sector aligned"
Assert-Contains $source "len\s*==\s*0UL" "zero-length image reads and writes must be no-ops"
Assert-Contains $source "w25qxx_erase_block" "store must erase W25Q sectors"
Assert-Contains $source "w25qxx_write" "store must write W25Q"
Assert-Contains $source "w25qxx_read" "store must read W25Q"

Write-Output "OTA store guard OK"
