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

function Get-NativeCCompiler {
    $compiler = Get-Command gcc, clang, cl -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $compiler) {
        return $null
    }
    return $compiler
}

function Invoke-CrcVectorCheck {
    param(
        [Parameter(Mandatory = $true)]
        $Compiler,
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("ota_crc_guard_" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempDir | Out-Null
    try {
        $testSource = Join-Path $tempDir "ota_crc_guard.c"
        $testExe = Join-Path $tempDir "ota_crc_guard.exe"
        $crcSource = Join-Path $Root "Ota\ota_crc32.c"
        $includeDir = Join-Path $Root "Ota"
        @'
#include "ota_crc32.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *text = "123456789";
    uint32_t empty_crc = ota_crc32_compute("", 0U);
    uint32_t text_crc = ota_crc32_compute(text, (uint32_t)strlen(text));

    if (empty_crc != 0x00000000UL) {
        fprintf(stderr, "empty crc mismatch: 0x%08lX\n", (unsigned long)empty_crc);
        return 1;
    }
    if (text_crc != 0xCBF43926UL) {
        fprintf(stderr, "123456789 crc mismatch: 0x%08lX\n", (unsigned long)text_crc);
        return 1;
    }
    return 0;
}
'@ | Set-Content -Path $testSource -Encoding ASCII

        if ($Compiler.Name -eq "cl.exe") {
            & $Compiler.Source /nologo /W4 /I"$includeDir" $testSource $crcSource /Fe"$testExe" | Out-String | Write-Verbose
        } else {
            & $Compiler.Source -std=c99 -Wall -Wextra -I"$includeDir" $testSource $crcSource -o $testExe
        }
        if ($LASTEXITCODE -ne 0) {
            throw "CRC vector host build failed with $($Compiler.Name)"
        }

        & $testExe
        if ($LASTEXITCODE -ne 0) {
            throw "CRC vector host check failed"
        }
    } finally {
        Remove-Item -Recurse -Force -Path $tempDir -ErrorAction SilentlyContinue
    }
}

$layout = Read-Text $layoutPath
$crcHeader = Read-Text $crcHeaderPath
$crcSource = Read-Text $crcSourcePath

Assert-Contains $layout "#define\s+OTA_BOOTLOADER_BASE\s+0x08000000UL" "bootloader base must stay at 0x08000000"
Assert-Contains $layout "#define\s+OTA_BOOTLOADER_SIZE\s+0x00007000UL" "bootloader code size must stay 28KB"
Assert-Contains $layout "#define\s+OTA_BOOT_STATE_PRIMARY_ADDR\s+0x08007000UL" "boot state primary page must start at 0x08007000"
Assert-Contains $layout "#define\s+OTA_BOOT_STATE_BACKUP_ADDR\s+0x08007800UL" "boot state backup page must start at 0x08007800"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_A_BASE\s+0x08008000UL" "slot A base must stay at 0x08008000"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_B_BASE\s+0x08024000UL" "slot B base must stay at 0x08024000"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_SIZE\s+0x0001C000UL" "app slot size must stay 112KB"
Assert-Contains $layout "#define\s+OTA_APP_SIZE\s+OTA_APP_SLOT_SIZE" "app size must map to the current slot size"
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
Assert-Contains $layout "#define\s+OTA_OLD_LFS_BLOCK_COUNT\s+512UL" "old LittleFS block count must match existing formatted filesystems"
Assert-Contains $layout "#define\s+OTA_TRANSFER_MAX_DATA_LEN\s+256UL" "OTA transfer data length must match binary 256-byte protocol chunks"
Assert-Contains $layout "#define\s+OTA_RESUME_CHECKPOINT_SIZE\s+0x00001000UL" "OTA resume checkpoint must be one W25Q sector"

Assert-Contains $crcHeader "uint32_t\s+ota_crc32_begin\s*\(void\)" "crc begin prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_update\s*\(uint32_t crc,\s*const void \*data,\s*uint32_t len\)" "crc update prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_finish\s*\(uint32_t crc\)" "crc finish prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_compute\s*\(const void \*data,\s*uint32_t len\)" "crc compute prototype missing"
Assert-Contains $crcSource "return\s+0xFFFFFFFFUL" "crc32 begin must initialize to all bits set"
Assert-Contains $crcSource "0xEDB88320UL" "crc32 must use reflected Ethernet polynomial"
Assert-Contains $crcSource "\^ 0xFFFFFFFFUL" "crc32 finish must xor final value"

$compiler = Get-NativeCCompiler
if ($null -eq $compiler) {
    Write-Warning "No native C compiler found; CRC vector check skipped"
} else {
    Invoke-CrcVectorCheck -Compiler $compiler -Root $root
}

Write-Output "OTA layout and CRC guard OK"
