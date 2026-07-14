$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$appALdPath = Join-Path $root "STM32F103XX_APP_A.ld"
$appBLdPath = Join-Path $root "STM32F103XX_APP_B.ld"
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

$appALd = Read-Text $appALdPath
$appBLd = Read-Text $appBLdPath
$bootLd = Read-Text $bootLdPath
$pio = Read-Text $pioPath
$system = Read-Text $systemPath

Assert-Contains $appALd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8008000,\s*LENGTH = 112K" "app A linker must start at 0x08008000 with 112K"
Assert-Contains $appBLd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8024000,\s*LENGTH = 112K" "app B linker must start at 0x08024000 with 112K"
Assert-Contains $bootLd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8000000,\s*LENGTH = 28K" "bootloader linker must stay inside first 28K"
Assert-Contains $pio "\[env:app_a\]" "PlatformIO app_a env missing"
Assert-Contains $pio "\[env:app_b\]" "PlatformIO app_b env missing"
Assert-Contains $pio "board_build\.ldscript\s*=\s*STM32F103XX_APP_A\.ld" "app_a env must use app A linker"
Assert-Contains $pio "board_build\.ldscript\s*=\s*STM32F103XX_APP_B\.ld" "app_b env must use app B linker"
Assert-Contains $pio "-DUSER_VECT_TAB_ADDRESS" "app env must enable vector table relocation"
Assert-Contains $pio "-DVECT_TAB_OFFSET=0x00008000U" "app env must set vector offset"
Assert-Contains $pio "-DVECT_TAB_OFFSET=0x00024000U" "app_b env must set vector offset"
Assert-Contains $pio "-DOTA_APP_SLOT_B" "app_b env must define slot B"
Assert-Contains $pio "\[env:bootloader\]" "PlatformIO bootloader env missing"
Assert-Contains $pio "board_build\.ldscript\s*=\s*STM32F103XX_BOOTLOADER\.ld" "bootloader env must use bootloader linker"
Assert-Contains $system "#ifndef\s+VECT_TAB_OFFSET" "system file must allow build flag override for VECT_TAB_OFFSET"
Assert-Contains $system "SCB->VTOR\s*=\s*VECT_TAB_BASE_ADDRESS\s*\|\s*VECT_TAB_OFFSET" "system file must set VTOR when enabled"

Write-Output "OTA partition guard OK"
