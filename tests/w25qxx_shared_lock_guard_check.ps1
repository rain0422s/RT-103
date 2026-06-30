$w25h = Get-Content -Raw "Lib/w25qxx/w25qxx.h"
$w25c = Get-Content -Raw "Lib/w25qxx/w25qxx.c"
$lfs = Get-Content -Raw "Lib/littlefs/lfs_port.c"
$ota = Get-Content -Raw "Ota/ota_store.c"

if ($w25h -notmatch "bool\s+w25qxx_lock\s*\(" -or
    $w25h -notmatch "void\s+w25qxx_unlock\s*\(") {
    throw "w25qxx driver must expose shared lock/unlock APIs"
}

if ($w25c -notmatch "xSemaphoreCreateBinary" -or
    $w25c -notmatch "xSemaphoreTake" -or
    $w25c -notmatch "xSemaphoreGive") {
    throw "w25qxx app build must implement a FreeRTOS semaphore-backed shared lock"
}

if ($lfs -notmatch "w25qxx_lock\s*\(" -or
    $lfs -notmatch "w25qxx_unlock\s*\(") {
    throw "LittleFS port must use the shared W25Q lock"
}

if ($ota -notmatch "w25qxx_lock\s*\(" -or
    $ota -notmatch "w25qxx_unlock\s*\(") {
    throw "OTA store must use the shared W25Q lock"
}

Write-Host "W25Q shared lock guard OK"
