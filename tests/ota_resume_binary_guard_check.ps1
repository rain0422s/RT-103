$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$layoutPath = Join-Path $root "Ota\ota_layout.h"
$appOtaHeaderPath = Join-Path $root "App\ota_update.h"
$appOtaPath = Join-Path $root "App\ota_update.c"
$uartPath = Join-Path $root "App\uart_forward.c"

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
$header = Read-Text $appOtaHeaderPath
$source = Read-Text $appOtaPath
$uart = Read-Text $uartPath

Assert-Contains $layout "#define\s+OTA_TRANSFER_MAX_DATA_LEN\s+256UL" "binary OTA path must raise max payload to 256 bytes"
Assert-Contains $layout "#define\s+OTA_RESUME_CHECKPOINT_SIZE\s+0x00001000UL" "resume checkpoint must be 4KB"
Assert-Contains $layout "#define\s+OTA_BINARY_MAGIC\s+0x4241544FUL" "binary OTA magic must be OTAB little-endian"

Assert-Contains $header "bool\s+ota_binary_process\s*\(const uint8_t \*data,\s*uint16_t len,\s*ota_reply_fn reply,\s*void \*ctx\)" "OTA binary process prototype missing"
Assert-Contains $source "typedef\s+struct\s*\{(?s).*uint32_t\s+magic;.*uint16_t\s+header_size;.*uint16_t\s+payload_len;.*uint32_t\s+sequence;.*uint32_t\s+offset;.*uint32_t\s+payload_crc32;" "binary OTA packet header missing"
Assert-Contains $source "OTA RESUME\?" "OTA RESUME? command missing"
Assert-Contains $source "OK OTA RESUME" "OTA RESUME? response missing"
Assert-Contains $source "ota_resume_load_manifest" "OTA resume must load manifest from flash"
Assert-Contains $source "ota_maybe_checkpoint_manifest" "OTA DATA must checkpoint receive progress"
Assert-Contains $source "OTA_BINARY_MAGIC" "binary parser must validate magic"
Assert-Contains $source "ota_binary_process" "binary OTA processor missing"
Assert-Contains $source "binary_sequence" "binary packets must track sequence"
Assert-Contains $source "payload_crc32" "binary packets must validate payload CRC"

Assert-Contains $uart "ota_binary_process" "UART forwarding must route binary OTA packets"
Assert-Contains $uart "OTA_BINARY_MAGIC" "UART forwarding must detect binary OTA magic"
Assert-Contains $uart "OK CMDS OTA HELP" "help must continue advertising OTA commands"

Write-Output "OTA resume and binary guard OK"
