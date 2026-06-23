$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$appLdPath = Join-Path $root "STM32F103XX_APP.ld"
$bootLdPath = Join-Path $root "STM32F103XX_BOOTLOADER.ld"
$pioPath = Join-Path $root "platformio.ini"
$systemPath = Join-Path $root "Core\Src\system_stm32f1xx.c"

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

$appLd = Read-Text $appLdPath
$bootLd = Read-Text $bootLdPath
$pio = Read-Text $pioPath
$system = Read-Text $systemPath

Assert-Contains $appLd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8008000,\s*LENGTH = 224K" "app linker must start at 0x08008000 with 224K"
Assert-Contains $bootLd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8000000,\s*LENGTH = 32K" "bootloader linker must stay inside first 32K"
Assert-Contains $pio "\[env:app\]" "PlatformIO app env missing"
Assert-Contains $pio "board_build\.ldscript\s*=\s*STM32F103XX_APP\.ld" "app env must use app linker"
Assert-Contains $pio "-DUSER_VECT_TAB_ADDRESS" "app env must enable vector table relocation"
Assert-Contains $pio "-DVECT_TAB_OFFSET=0x00008000U" "app env must set vector offset"
Assert-Contains $pio "\[env:bootloader\]" "PlatformIO bootloader env missing"
Assert-Contains $pio "board_build\.ldscript\s*=\s*STM32F103XX_BOOTLOADER\.ld" "bootloader env must use bootloader linker"
Assert-Contains $system "#ifndef\s+VECT_TAB_OFFSET" "system file must allow build flag override for VECT_TAB_OFFSET"
Assert-Contains $system "SCB->VTOR\s*=\s*VECT_TAB_BASE_ADDRESS\s*\|\s*VECT_TAB_OFFSET" "system file must set VTOR when enabled"

Write-Output "OTA partition guard OK"
