$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$layoutPath = Join-Path $root "Ota\ota_layout.h"
$crcHeaderPath = Join-Path $root "Ota\ota_crc32.h"
$crcSourcePath = Join-Path $root "Ota\ota_crc32.c"

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

$layout = Read-Text $layoutPath
$crcHeader = Read-Text $crcHeaderPath
$crcSource = Read-Text $crcSourcePath

Assert-Contains $layout "#define\s+OTA_BOOTLOADER_BASE\s+0x08000000UL" "bootloader base must stay at 0x08000000"
Assert-Contains $layout "#define\s+OTA_BOOTLOADER_SIZE\s+0x00008000UL" "bootloader size must stay 32KB"
Assert-Contains $layout "#define\s+OTA_APP_BASE\s+0x08008000UL" "app base must stay at 0x08008000"
Assert-Contains $layout "#define\s+OTA_APP_SIZE\s+0x00038000UL" "app size must stay 224KB"
Assert-Contains $layout "#define\s+OTA_EXT_FLASH_SIZE\s+0x00200000UL" "W25Q16 external flash size must be 2MB"
Assert-Contains $layout "#define\s+OTA_MANIFEST_ADDR\s+0x00000000UL" "manifest must live at W25Q address 0"
Assert-Contains $layout "#define\s+OTA_IMAGE_ADDR\s+0x00001000UL" "image slot must start after manifest sector"
Assert-Contains $layout "#define\s+OTA_IMAGE_SLOT_SIZE\s+0x00040000UL" "image slot must reserve 256KB"
Assert-Contains $layout "#define\s+OTA_LFS_BASE\s+0x00041000UL" "LittleFS must start after OTA slot"
Assert-Contains $layout "#define\s+OTA_LFS_OFFSET_BLOCKS\s+65UL" "LittleFS block offset must match 0x41000 / 4096"
Assert-Contains $layout "#define\s+OTA_LFS_BLOCK_COUNT\s+447UL" "LittleFS block count must fit W25Q16"
Assert-Contains $layout "#define\s+OTA_OLD_LFS_OFFSET_BLOCKS\s+3UL" "old LittleFS offset must remain available for migration"

Assert-Contains $crcHeader "uint32_t\s+ota_crc32_begin\s*\(void\)" "crc begin prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_update\s*\(uint32_t crc,\s*const void \*data,\s*uint32_t len\)" "crc update prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_finish\s*\(uint32_t crc\)" "crc finish prototype missing"
Assert-Contains $crcSource "0xEDB88320UL" "crc32 must use reflected Ethernet polynomial"
Assert-Contains $crcSource "\^ 0xFFFFFFFFUL" "crc32 finish must xor final value"

Write-Output "OTA layout and CRC guard OK"
