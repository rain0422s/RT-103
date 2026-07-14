$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$jumpHeaderPath = Join-Path $root "Bootloader\boot_jump.h"
$jumpSourcePath = Join-Path $root "Bootloader\boot_jump.c"
$mainPath = Join-Path $root "Bootloader\main.c"
$pioPath = Join-Path $root "platformio.ini"
$w25qPath = Join-Path $root "Lib\w25qxx\w25qxx.c"

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

$jumpHeader = Read-Text $jumpHeaderPath
$jumpSource = Read-Text $jumpSourcePath
$main = Read-Text $mainPath
$pio = Read-Text $pioPath
$w25q = Read-Text $w25qPath

Assert-Contains $jumpHeader "bool\s+boot_app_is_valid\s*\(void\)" "app validity prototype missing"
Assert-Contains $jumpHeader "bool\s+boot_app_is_valid_at\s*\(uint32_t app_base\)" "slot app validity prototype missing"
Assert-Contains $jumpHeader "void\s+boot_jump_to_app\s*\(void\)" "jump prototype missing"
Assert-Contains $jumpHeader "void\s+boot_jump_to_slot\s*\(uint32_t app_base\)" "slot jump prototype missing"
Assert-Contains $jumpSource "OTA_APP_SLOT_A_BASE" "jump code must validate slot A base"
Assert-Contains $jumpSource "OTA_APP_SLOT_B_BASE" "jump code must validate slot B base"
Assert-Contains $jumpSource "OTA_SRAM_BASE" "jump code must validate MSP in SRAM"
Assert-Contains $jumpSource "OTA_APP_SLOT_SIZE" "jump code must validate reset handler inside selected slot"
Assert-Contains $jumpSource "__set_MSP" "jump code must set MSP"
Assert-Contains $jumpSource "SCB->VTOR" "jump code must set vector table"
Assert-Contains $main "boot_jump_to_slot" "bootloader main must jump to selected app slot"
Assert-Contains $main "boot_gpio_init" "bootloader must use local GPIO init before W25Q CS"
Assert-Contains $main "boot_spi1_init" "bootloader must use local SPI1 init"
Assert-Contains $pio "-DW25QXX_USE_FREERTOS=0" "bootloader must build W25Q driver without FreeRTOS"
Assert-Contains $w25q "#if\s+W25QXX_USE_FREERTOS" "W25Q driver must support non-FreeRTOS bootloader builds"

Write-Output "OTA boot jump guard OK"
