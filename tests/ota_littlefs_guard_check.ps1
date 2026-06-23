$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourcePath = Join-Path $root "Lib\littlefs\lfs_port.c"
$headerPath = Join-Path $root "Lib\littlefs\lfs_port.h"
$platformioPath = Join-Path $root "platformio.ini"

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

function Assert-NotContains($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) {
        throw $Message
    }
}

$source = Read-Text $sourcePath
$header = Read-Text $headerPath
$platformio = Read-Text $platformioPath
if ($platformio -notmatch '(?s)\[env:genericSTM32F103RC\](.*?)\r?\n\[env:app\]') {
    throw "genericSTM32F103RC environment section missing"
}
$genericEnv = $Matches[1]

Assert-Contains $source '#include "ota_layout.h"' "lfs port must include OTA layout"
Assert-Contains $source "s_lfs_offset_blocks" "lfs callbacks must use a runtime offset for migration"
Assert-Contains $source "OTA_LFS_OFFSET_BLOCKS" "new lfs offset must come from OTA layout"
Assert-Contains $source "OTA_OLD_LFS_OFFSET_BLOCKS" "old lfs offset must remain readable for migration"
Assert-Contains $source "OTA_OLD_LFS_BLOCK_COUNT" "old lfs mount must use the previous 512-block geometry"
Assert-Contains $source "lfs_w25qxx_old_cfg" "old lfs migration config missing"
Assert-Contains $source "\.block_count\s*=\s*OTA_LFS_BLOCK_COUNT" "new lfs block count must fit W25Q16 after OTA slot"
Assert-Contains $source "battery_hist" "battery history must be migrated"
Assert-Contains $source "uptime_ckpt" "uptime checkpoint must be migrated"
Assert-Contains $source "boot_count" "boot count must be migrated"
Assert-Contains $source "lfs_migrate_from_old_offset" "migration function missing"
Assert-Contains $header "int\s+lfs_migrate_from_old_offset\s*\(void\)" "migration prototype missing"
Assert-Contains $genericEnv '-I\s+"Ota"' "base build must expose Ota headers to Lib/littlefs"
Assert-NotContains $source "#define\s+OFFSETBLOCK\s+3" "fixed old offset macro must be removed"

Write-Output "OTA LittleFS guard OK"
