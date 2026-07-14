$layout = Get-Content -Raw "Ota/ota_layout.h"
$uart = Get-Content -Raw "App/uart_forward.c"
$ota = Get-Content -Raw "App/ota_update.c"

if ($layout -notmatch "#define\s+OTA_TRANSFER_MAX_DATA_LEN\s+256UL") {
    throw "OTA binary transfer chunk should be 256 bytes"
}

if ($uart -notmatch "#define\s+USART_LEN\s+320" -or
    $uart -notmatch "#define\s+UART_LINE_LEN\s+192") {
    throw "UART buffers must fit 256-byte binary OTA packets and legacy text lines"
}

if ($uart -notmatch "#define\s+UART_FORWARD_TASK_STACK_WORDS\s+512" -or
    $uart -notmatch "xTaskCreate\s*\(\s*uart_forward_task,\s*""uart_fwd"",\s*UART_FORWARD_TASK_STACK_WORDS") {
    throw "UART forward task stack must cover OTA packet buffering and command formatting"
}

$otaDataMatch = [regex]::Match($ota, "(?s)static\s+bool\s+ota_data\s*\([^)]*\)\s*\{.*?\n\}")
if (-not $otaDataMatch.Success) {
    throw "ota_data function not found"
}

if ($otaDataMatch.Value -match "ota_store_write_manifest\s*\(") {
    throw "ota_data must not erase/write manifest directly for every chunk"
}

Write-Host "OTA transfer efficiency guard OK"
