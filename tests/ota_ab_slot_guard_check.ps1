$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$layoutPath = Join-Path $root "Ota\ota_layout.h"
$bootStateHeaderPath = Join-Path $root "Ota\ota_boot_state.h"
$bootStateSourcePath = Join-Path $root "Ota\ota_boot_state.c"
$bootJumpPath = Join-Path $root "Bootloader\boot_jump.c"
$bootMainPath = Join-Path $root "Bootloader\main.c"
$bootLdPath = Join-Path $root "STM32F103XX_BOOTLOADER.ld"
$appALdPath = Join-Path $root "STM32F103XX_APP_A.ld"
$appBLdPath = Join-Path $root "STM32F103XX_APP_B.ld"
$pioPath = Join-Path $root "platformio.ini"
$appOtaPath = Join-Path $root "App\ota_update.c"
$appMainPath = Join-Path $root "Core\Src\main.c"

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
$bootStateHeader = Read-Text $bootStateHeaderPath
$bootStateSource = Read-Text $bootStateSourcePath
$bootJump = Read-Text $bootJumpPath
$bootMain = Read-Text $bootMainPath
$bootLd = Read-Text $bootLdPath
$appALd = Read-Text $appALdPath
$appBLd = Read-Text $appBLdPath
$pio = Read-Text $pioPath
$appOta = Read-Text $appOtaPath
$appMain = Read-Text $appMainPath

Assert-Contains $layout "#define\s+OTA_BOOTLOADER_SIZE\s+0x00007000UL" "bootloader code size must reserve boot-state pages"
Assert-Contains $layout "#define\s+OTA_BOOT_STATE_PRIMARY_ADDR\s+0x08007000UL" "primary boot state page must start at 0x08007000"
Assert-Contains $layout "#define\s+OTA_BOOT_STATE_BACKUP_ADDR\s+0x08007800UL" "backup boot state page must start at 0x08007800"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_A_BASE\s+0x08008000UL" "slot A base must stay at 0x08008000"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_B_BASE\s+0x08024000UL" "slot B base must be 0x08024000"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_SIZE\s+0x0001C000UL" "each app slot must be 112KB"
Assert-Contains $layout "#define\s+OTA_APP_SLOT_COUNT\s+2UL" "layout must declare two app slots"
Assert-Contains $layout "#define\s+OTA_APP_B_VECT_TAB_OFFSET\s+0x00024000U" "slot B vector table offset must be declared"

Assert-Contains $bootStateHeader "typedef\s+enum\s*\{(?s).*OTA_SLOT_A\s*=\s*0" "boot state must define slot A"
Assert-Contains $bootStateHeader "OTA_SLOT_B\s*=\s*1" "boot state must define slot B"
Assert-Contains $bootStateHeader "ota_boot_state_confirm_current" "app must be able to confirm current slot"
Assert-Contains $bootStateHeader "ota_boot_state_slot_base" "bootloader must be able to resolve slot base"
Assert-Contains $bootStateSource "HAL_FLASHEx_Erase" "boot state must erase internal flash pages"
Assert-Contains $bootStateSource "HAL_FLASH_Program" "boot state must program internal flash pages"
Assert-Contains $bootStateSource "OTA_BOOT_STATE_PRIMARY_ADDR" "boot state must use primary state page"
Assert-Contains $bootStateSource "OTA_BOOT_STATE_BACKUP_ADDR" "boot state must use backup state page"

Assert-Contains $bootLd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8000000,\s*LENGTH = 28K" "bootloader linker must reserve 28KB for code"
Assert-Contains $appALd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8008000,\s*LENGTH = 112K" "app A linker must use slot A"
Assert-Contains $appBLd "FLASH \(rx\)\s*:\s*ORIGIN = 0x8024000,\s*LENGTH = 112K" "app B linker must use slot B"
Assert-Contains $pio "\[env:app_a\]" "PlatformIO app_a env missing"
Assert-Contains $pio "\[env:app_b\]" "PlatformIO app_b env missing"
Assert-Contains $pio "STM32F103XX_APP_A\.ld" "app_a must use app A linker"
Assert-Contains $pio "STM32F103XX_APP_B\.ld" "app_b must use app B linker"
Assert-Contains $pio "-DOTA_APP_SLOT_B" "app_b must define slot B build flag"
Assert-Contains $pio "-DVECT_TAB_OFFSET=0x00024000U" "app_b must set slot B vector offset"

Assert-Contains $bootJump "boot_jump_to_slot" "bootloader jump code must accept a slot base"
Assert-Contains $bootJump "SCB->VTOR\s*=\s*app_base" "bootloader must set VTOR to selected slot base"
Assert-Contains $bootMain "ota_boot_state_select_boot_slot" "bootloader main must select boot slot from boot state"
Assert-Contains $appOta "ota_update_confirm_boot" "app OTA module must expose boot confirmation"
Assert-Contains $appMain "ota_update_confirm_boot" "app main must confirm boot after initialization"

Write-Output "OTA A/B slot guard OK"
