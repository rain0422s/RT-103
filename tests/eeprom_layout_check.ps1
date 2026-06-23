$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$header = Join-Path $root "App\eeprom_layout.h"
$text = Get-Content -Raw -LiteralPath $header

function Get-MacroInt([string]$name) {
    $pattern = "(?m)^#define\s+$name\s+\(?([0-9A-Fa-fxXu]+)\)?"
    $m = [regex]::Match($text, $pattern)
    if (-not $m.Success) {
        throw "missing macro: $name"
    }
    $raw = $m.Groups[1].Value.TrimEnd("u", "U")
    if ($raw.StartsWith("0x") -or $raw.StartsWith("0X")) {
        return [Convert]::ToInt32($raw, 16)
    }
    return [int]$raw
}

$configSize = Get-MacroInt "EEPROM_CONFIG_SIZE"
$recordSize = Get-MacroInt "EEPROM_CONFIG_RECORD_SIZE"
$slotCount = Get-MacroInt "EEPROM_CONFIG_SLOT_COUNT"
$slot0 = Get-MacroInt "EEPROM_OFFSET_CONFIG_SLOT0"
$slot1 = Get-MacroInt "EEPROM_OFFSET_CONFIG_SLOT1"
$calib = Get-MacroInt "EEPROM_OFFSET_CALIB"
$stats = Get-MacroInt "EEPROM_OFFSET_STATS"
$eepromSize = Get-MacroInt "EEPROM_TOTAL_SIZE"
$testOffset = Get-MacroInt "EEPROM_OFFSET_TEST"

if ($configSize -gt $calib) { throw "config overlaps calib area" }
if ($slotCount -ne 2) { throw "expected two config slots" }
if ($recordSize -lt ($configSize + 12)) { throw "record has no room for seq/crc/header" }
if ($slot0 -ne $stats) { throw "slot0 must start at stats area" }
if ($slot1 -ne ($slot0 + $recordSize)) { throw "slot1 must follow slot0" }
if (($slot1 + $recordSize) -gt $eepromSize) { throw "journal exceeds EEPROM size" }
if ($testOffset -lt ($slot1 + $recordSize)) { throw "test area overlaps journal" }
if (($testOffset + 8) -gt $eepromSize) { throw "test area exceeds EEPROM size" }

Write-Host "EEPROM journal layout OK"
