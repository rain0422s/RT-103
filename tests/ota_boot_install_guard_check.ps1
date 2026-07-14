$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$flashHeaderPath = Join-Path $root "Bootloader\boot_flash.h"
$flashSourcePath = Join-Path $root "Bootloader\boot_flash.c"
$otaHeaderPath = Join-Path $root "Bootloader\boot_ota.h"
$otaSourcePath = Join-Path $root "Bootloader\boot_ota.c"
$mainPath = Join-Path $root "Bootloader\main.c"

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

$flashHeader = Read-Text $flashHeaderPath
$flashSource = Read-Text $flashSourcePath
$otaHeader = Read-Text $otaHeaderPath
$otaSource = Read-Text $otaSourcePath
$main = Read-Text $mainPath

Assert-Contains $flashHeader "boot_flash_erase_app" "erase app prototype missing"
Assert-Contains $flashHeader "boot_flash_erase_slot" "erase slot prototype missing"
Assert-Contains $flashHeader "boot_flash_program" "program prototype missing"
Assert-Contains $flashSource "HAL_FLASHEx_Erase" "bootloader must erase app pages"
Assert-Contains $flashSource "HAL_FLASH_Program" "bootloader must program internal flash"
Assert-Contains $flashSource "FLASH_TYPEPROGRAM_HALFWORD" "STM32F1 must program halfwords"
Assert-Contains $otaHeader "boot_ota_apply_if_pending" "OTA apply prototype missing"
Assert-Contains $otaSource "ota_manifest_is_pending" "bootloader must only apply pending manifest"
Assert-Contains $otaSource "ota_store_read_image" "bootloader must read image slot"
Assert-Contains $otaSource "ota_crc32_update" "bootloader must verify image CRC"
Assert-Contains $otaSource "boot_flash_erase_slot" "bootloader must erase target slot before programming"
Assert-Contains $otaSource "boot_flash_program" "bootloader must program app flash"
Assert-Contains $otaSource "boot_ota_app_crc_ok" "bootloader must verify programmed internal flash"
Assert-Contains $otaSource "\(const uint8_t \*\)app_base" "internal flash CRC must read from selected app slot base"
Assert-Contains $otaSource "OTA_MANIFEST_STATE_APPLIED" "bootloader must mark successful apply"
Assert-Contains $otaSource "ota_manifest_set_failure" "bootloader must mark failed apply with a reason code"
Assert-Contains $main "boot_ota_apply_if_pending" "bootloader main must invoke OTA apply before app jump"

Write-Output "OTA boot install guard OK"
