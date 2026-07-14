$ErrorActionPreference = "Stop"

$bootOtaPath = Join-Path $PSScriptRoot "..\Bootloader\boot_ota.c"
$bootOta = Get-Content -Raw -Path $bootOtaPath

function Assert-Contains($Text, $Pattern, $Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Contains $bootOta "\bboot_ota_image_header_ok\s*\(" "bootloader must preflight the staged app vector table before CRC/program"
Assert-Contains $bootOta "ota_store_read_image\s*\(\s*0UL\s*," "staged app preflight must read the image vector table first"
Assert-Contains $bootOta "OTA_SRAM_BASE" "staged app preflight must validate MSP is in SRAM"
Assert-Contains $bootOta "OTA_SRAM_END" "staged app preflight must validate MSP is in SRAM"
Assert-Contains $bootOta "app_base" "staged app preflight must validate reset vector against target slot base"
Assert-Contains $bootOta "OTA_APP_SLOT_SIZE" "staged app preflight must validate reset vector inside target slot"

$pendingCheck = $bootOta.IndexOf("if (!ota_manifest_is_pending")
$headerCheck = $bootOta.IndexOf("if (!boot_ota_image_header_ok")
$imageCrc = $bootOta.IndexOf("if (!boot_ota_image_crc_ok")
$program = $bootOta.IndexOf("if (!boot_ota_program_app")

if ($pendingCheck -lt 0 -or $headerCheck -lt 0 -or $imageCrc -lt 0 -or $program -lt 0) {
    throw "bootloader OTA apply flow is missing expected stages"
}
if ($headerCheck -lt $pendingCheck -or $headerCheck -gt $imageCrc -or $headerCheck -gt $program) {
    throw "staged app vector preflight must run after pending check and before CRC/program"
}

Write-Output "OTA boot image header guard OK"
