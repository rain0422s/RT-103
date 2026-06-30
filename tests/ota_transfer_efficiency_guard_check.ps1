$layout = Get-Content -Raw "Ota/ota_layout.h"
$uart = Get-Content -Raw "App/uart_forward.c"
$ota = Get-Content -Raw "App/ota_update.c"

if ($layout -notmatch "#define\s+OTA_TRANSFER_MAX_DATA_LEN\s+64UL") {
    throw "OTA transfer chunk should be 64 bytes to keep serial OTA practical"
}

if ($uart -notmatch "#define\s+USART_LEN\s+192" -or
    $uart -notmatch "#define\s+UART_LINE_LEN\s+192") {
    throw "UART buffers must fit 64-byte OTA DATA lines"
}

$otaDataMatch = [regex]::Match($ota, "(?s)static\s+bool\s+ota_data\s*\([^)]*\)\s*\{.*?\n\}")
if (-not $otaDataMatch.Success) {
    throw "ota_data function not found"
}

if ($otaDataMatch.Value -match "ota_store_write_manifest\s*\(") {
    throw "ota_data must not erase/write manifest for every chunk"
}

Write-Host "OTA transfer efficiency guard OK"
