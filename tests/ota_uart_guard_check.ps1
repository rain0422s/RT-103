$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "App\ota_update.h"
$sourcePath = Join-Path $root "App\ota_update.c"
$uartPath = Join-Path $root "App\uart_forward.c"
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

$header = Read-Text $headerPath
$source = Read-Text $sourcePath
$uart = Read-Text $uartPath
$platformio = Read-Text $platformioPath
if ($platformio -notmatch '(?s)\[env:genericSTM32F103RC\](.*?)\r?\n\[env:app\]') {
    throw "genericSTM32F103RC environment section missing"
}
$genericEnv = $Matches[1]

Assert-Contains $header "typedef\s+void\s+\(\*ota_reply_fn\)" "OTA reply callback type missing"
Assert-Contains $header "bool\s+ota_command_process\s*\(" "OTA command process prototype missing"
Assert-Contains $source "OTA BEGIN" "OTA BEGIN handler missing"
Assert-Contains $source "OTA DATA" "OTA DATA handler missing"
Assert-Contains $source "OTA END" "OTA END handler missing"
Assert-Contains $source "OTA APPLY" "OTA APPLY handler missing"
Assert-Contains $source "OTA ABORT" "OTA ABORT handler missing"
Assert-Contains $source "OTA STATUS\?" "OTA STATUS handler missing"
Assert-Contains $source "OTA_TRANSFER_MAX_DATA_LEN" "OTA data block length must be bounded"
Assert-Contains $source "if\s*\(hi\s*<\s*0\)\s*return false;" "hex decoder must reject a missing high nibble before reading low nibble"
Assert-Contains $source "if\s*\(lo\s*<\s*0\)\s*return false;" "hex decoder must reject a missing low nibble before using the byte"
Assert-Contains $source "HAL_NVIC_SystemReset" "OTA APPLY must reset the MCU"
Assert-Contains $source "ota_store_write_image" "OTA DATA must write to image slot"
Assert-Contains $source "ota_store_write_manifest" "OTA must write manifest state"
Assert-Contains $source "ota_crc32_update" "OTA must calculate CRC32"
Assert-Contains $uart '#include "ota_update.h"' "uart_forward must include OTA update module"
Assert-Contains $uart 'strncmp\(line,\s*"OTA ",\s*4\)' "uart_forward must route OTA commands"
Assert-Contains $uart "ota_command_process" "uart_forward must call ota_command_process"
Assert-Contains $uart "OK CMDS OTA HELP" "help must advertise OTA commands"
Assert-Contains $genericEnv '\+<Ota/>' "base build must compile OTA sources used by App/ota_update.c"

Write-Output "OTA UART guard OK"
