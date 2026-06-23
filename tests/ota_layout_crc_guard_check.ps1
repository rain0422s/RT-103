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
Assert-Contains $layout "#define\s+OTA_FLASH_END\s+0x08040000UL" "flash end must match STM32F103RCT6 256KB boundary"
Assert-Contains $layout "#define\s+OTA_SRAM_BASE\s+0x20000000UL" "SRAM base must stay at 0x20000000"
Assert-Contains $layout "#define\s+OTA_SRAM_SIZE\s+0x0000C000UL" "SRAM size must stay 48KB"
Assert-Contains $layout "#define\s+OTA_SRAM_END\s+\(OTA_SRAM_BASE \+ OTA_SRAM_SIZE\)" "SRAM end must derive from base plus size"
Assert-Contains $layout "#define\s+OTA_EXT_FLASH_SIZE\s+0x00200000UL" "W25Q16 external flash size must be 2MB"
Assert-Contains $layout "#define\s+OTA_W25Q_SECTOR_SIZE\s+0x00001000UL" "W25Q sector size must stay 4KB"
Assert-Contains $layout "#define\s+OTA_W25Q_PAGE_SIZE\s+0x00000100UL" "W25Q page size must stay 256 bytes"
Assert-Contains $layout "#define\s+OTA_MANIFEST_ADDR\s+0x00000000UL" "manifest must live at W25Q address 0"
Assert-Contains $layout "#define\s+OTA_IMAGE_ADDR\s+0x00001000UL" "image slot must start after manifest sector"
Assert-Contains $layout "#define\s+OTA_IMAGE_SLOT_SIZE\s+0x00040000UL" "image slot must reserve 256KB"
Assert-Contains $layout "#define\s+OTA_LFS_BASE\s+0x00041000UL" "LittleFS must start after OTA slot"
Assert-Contains $layout "#define\s+OTA_LFS_BLOCK_SIZE\s+0x00001000UL" "LittleFS block size must stay 4KB"
Assert-Contains $layout "#define\s+OTA_LFS_OFFSET_BLOCKS\s+65UL" "LittleFS block offset must match 0x41000 / 4096"
Assert-Contains $layout "#define\s+OTA_LFS_BLOCK_COUNT\s+447UL" "LittleFS block count must fit W25Q16"
Assert-Contains $layout "#define\s+OTA_OLD_LFS_OFFSET_BLOCKS\s+3UL" "old LittleFS offset must remain available for migration"
Assert-Contains $layout "#define\s+OTA_TRANSFER_MAX_DATA_LEN\s+16UL" "OTA transfer data length must match 16-byte protocol chunks"

Assert-Contains $crcHeader "uint32_t\s+ota_crc32_begin\s*\(void\)" "crc begin prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_update\s*\(uint32_t crc,\s*const void \*data,\s*uint32_t len\)" "crc update prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_finish\s*\(uint32_t crc\)" "crc finish prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_compute\s*\(const void \*data,\s*uint32_t len\)" "crc compute prototype missing"
Assert-Contains $crcSource "return\s+0xFFFFFFFFUL" "crc32 begin must initialize to all bits set"
Assert-Contains $crcSource "0xEDB88320UL" "crc32 must use reflected Ethernet polynomial"
Assert-Contains $crcSource "\^ 0xFFFFFFFFUL" "crc32 finish must xor final value"
Assert-Contains $crcSource "return\s+ota_crc32_finish\s*\(\s*ota_crc32_update\s*\(\s*ota_crc32_begin\s*\(\s*\)\s*,\s*data\s*,\s*len\s*\)\s*\)" "crc compute must wrap begin/update/finish"

Write-Output "OTA layout and CRC guard OK"
