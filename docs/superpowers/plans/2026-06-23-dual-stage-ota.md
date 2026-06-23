# Dual-Stage OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete two-stage OTA path for RT-103: USART1 receives an app image into W25Q16, then a bootloader validates and installs it into the internal Flash app partition.

**Architecture:** Shared OTA layout, CRC, manifest, and W25Q store code live under `Ota/` so both app and bootloader use the same constants. The app remains the FreeRTOS firmware at `0x08008000`; the bootloader is a small non-RTOS image at `0x08000000` that checks a pending manifest, rewrites the app partition, and jumps to the app. W25Q16 reserves a raw OTA slot before LittleFS; LittleFS is moved and known user files are migrated once.

**Tech Stack:** STM32F103RCT6, PlatformIO, STM32 HAL, FreeRTOS app, SPI1 W25Q16, LittleFS, USART1 text protocol, PowerShell guard tests.

---

## File Structure

- Create `Ota/ota_layout.h`: single source of truth for internal Flash and W25Q16 OTA/LittleFS addresses.
- Create `Ota/ota_crc32.h` and `Ota/ota_crc32.c`: small software CRC32 used by app and bootloader.
- Create `Ota/ota_manifest.h` and `Ota/ota_manifest.c`: fixed manifest format, state transitions, and manifest CRC.
- Create `Ota/ota_store.h` and `Ota/ota_store.c`: W25Q16 manifest/image read, write, erase helpers shared by app and bootloader.
- Create `App/ota_update.h` and `App/ota_update.c`: USART1 OTA command parser and app-side receive state machine.
- Modify `App/uart_forward.c`: route `OTA ...` commands to `ota_update`.
- Modify `Lib/littlefs/lfs_port.c`: move LittleFS to the new offset and migrate known old-offset files.
- Modify `Lib/littlefs/lfs_port.h`: expose migration/init helpers only if needed by storage init.
- Modify `Core/Src/system_stm32f1xx.c`: allow build flags to override vector table offset.
- Create `STM32F103XX_APP.ld`: app linker script at `0x08008000`.
- Create `STM32F103XX_BOOTLOADER.ld`: bootloader linker script limited to 32 KB.
- Modify `platformio.ini`: add `app` and `bootloader` envs while keeping the existing env as a compatibility build until the app env is verified.
- Create `Bootloader/main.c`: bootloader entry, minimal clock/peripheral init, OTA check, jump/error loop.
- Create `Bootloader/boot_jump.h` and `Bootloader/boot_jump.c`: app vector validation and safe jump.
- Create `Bootloader/boot_flash.h` and `Bootloader/boot_flash.c`: internal Flash erase/program/verify helpers.
- Create `Bootloader/boot_ota.h` and `Bootloader/boot_ota.c`: pending manifest validation and OTA install flow.
- Create or extend PowerShell guard tests under `tests/` for each task.

## Constants Chosen

Use these exact values unless hardware evidence proves the external flash is not W25Q16:

```c
#define OTA_BOOTLOADER_BASE        0x08000000UL
#define OTA_BOOTLOADER_SIZE        0x00008000UL
#define OTA_APP_BASE               0x08008000UL
#define OTA_APP_SIZE               0x00038000UL
#define OTA_FLASH_END              0x08040000UL

#define OTA_EXT_FLASH_SIZE         0x00200000UL
#define OTA_W25Q_SECTOR_SIZE       0x00001000UL
#define OTA_MANIFEST_ADDR          0x00000000UL
#define OTA_IMAGE_ADDR             0x00001000UL
#define OTA_IMAGE_SLOT_SIZE        0x00040000UL
#define OTA_LFS_BASE               0x00041000UL
#define OTA_LFS_BLOCK_SIZE         0x00001000UL
#define OTA_LFS_OFFSET_BLOCKS      65UL
#define OTA_LFS_BLOCK_COUNT        447UL
#define OTA_OLD_LFS_OFFSET_BLOCKS  3UL
```

`OTA_LFS_BLOCK_COUNT` is 447 because W25Q16 is 2 MB and `0x00200000 - 0x00041000` leaves 447 full 4 KB blocks.

---

### Task 1: Shared Layout And CRC32

**Files:**
- Create: `tests/ota_layout_crc_guard_check.ps1`
- Create: `Ota/ota_layout.h`
- Create: `Ota/ota_crc32.h`
- Create: `Ota/ota_crc32.c`

- [ ] **Step 1: Write the failing guard test**

Create `tests/ota_layout_crc_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$layoutPath = Join-Path $root "Ota\ota_layout.h"
$crcHeaderPath = Join-Path $root "Ota\ota_crc32.h"
$crcSourcePath = Join-Path $root "Ota\ota_crc32.c"

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
$crcHeader = Read-Text $crcHeaderPath
$crcSource = Read-Text $crcSourcePath

Assert-Contains $layout "#define\s+OTA_BOOTLOADER_BASE\s+0x08000000UL" "bootloader base must stay at 0x08000000"
Assert-Contains $layout "#define\s+OTA_BOOTLOADER_SIZE\s+0x00008000UL" "bootloader size must stay 32KB"
Assert-Contains $layout "#define\s+OTA_APP_BASE\s+0x08008000UL" "app base must stay at 0x08008000"
Assert-Contains $layout "#define\s+OTA_APP_SIZE\s+0x00038000UL" "app size must stay 224KB"
Assert-Contains $layout "#define\s+OTA_EXT_FLASH_SIZE\s+0x00200000UL" "W25Q16 external flash size must be 2MB"
Assert-Contains $layout "#define\s+OTA_MANIFEST_ADDR\s+0x00000000UL" "manifest must live at W25Q address 0"
Assert-Contains $layout "#define\s+OTA_IMAGE_ADDR\s+0x00001000UL" "image slot must start after manifest sector"
Assert-Contains $layout "#define\s+OTA_IMAGE_SLOT_SIZE\s+0x00040000UL" "image slot must reserve 256KB"
Assert-Contains $layout "#define\s+OTA_LFS_BASE\s+0x00041000UL" "LittleFS must start after OTA slot"
Assert-Contains $layout "#define\s+OTA_LFS_OFFSET_BLOCKS\s+65UL" "LittleFS block offset must match 0x41000 / 4096"
Assert-Contains $layout "#define\s+OTA_LFS_BLOCK_COUNT\s+447UL" "LittleFS block count must fit W25Q16"
Assert-Contains $layout "#define\s+OTA_OLD_LFS_OFFSET_BLOCKS\s+3UL" "old LittleFS offset must remain available for migration"

Assert-Contains $crcHeader "uint32_t\s+ota_crc32_begin\s*\(void\)" "crc begin prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_update\s*\(uint32_t crc,\s*const void \*data,\s*uint32_t len\)" "crc update prototype missing"
Assert-Contains $crcHeader "uint32_t\s+ota_crc32_finish\s*\(uint32_t crc\)" "crc finish prototype missing"
Assert-Contains $crcSource "0xEDB88320UL" "crc32 must use reflected Ethernet polynomial"
Assert-Contains $crcSource "\^ 0xFFFFFFFFUL" "crc32 finish must xor final value"

Write-Output "OTA layout and CRC guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_layout_crc_guard_check.ps1
```

Expected: FAIL with `Missing required file: ...\Ota\ota_layout.h`.

- [ ] **Step 3: Add shared layout constants**

Create `Ota/ota_layout.h`:

```c
#ifndef OTA_LAYOUT_H
#define OTA_LAYOUT_H

#include <stdint.h>

#define OTA_BOOTLOADER_BASE        0x08000000UL
#define OTA_BOOTLOADER_SIZE        0x00008000UL
#define OTA_APP_BASE               0x08008000UL
#define OTA_APP_SIZE               0x00038000UL
#define OTA_FLASH_END              0x08040000UL

#define OTA_SRAM_BASE              0x20000000UL
#define OTA_SRAM_SIZE              0x0000C000UL
#define OTA_SRAM_END               (OTA_SRAM_BASE + OTA_SRAM_SIZE)

#define OTA_EXT_FLASH_SIZE         0x00200000UL
#define OTA_W25Q_SECTOR_SIZE       0x00001000UL
#define OTA_W25Q_PAGE_SIZE         0x00000100UL
#define OTA_MANIFEST_ADDR          0x00000000UL
#define OTA_IMAGE_ADDR             0x00001000UL
#define OTA_IMAGE_SLOT_SIZE        0x00040000UL
#define OTA_LFS_BASE               0x00041000UL
#define OTA_LFS_BLOCK_SIZE         0x00001000UL
#define OTA_LFS_OFFSET_BLOCKS      65UL
#define OTA_LFS_BLOCK_COUNT        447UL
#define OTA_OLD_LFS_OFFSET_BLOCKS  3UL

#define OTA_TRANSFER_MAX_DATA_LEN  16UL

#endif
```

- [ ] **Step 4: Add CRC32 primitives**

Create `Ota/ota_crc32.h`:

```c
#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stdint.h>

uint32_t ota_crc32_begin(void);
uint32_t ota_crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t ota_crc32_finish(uint32_t crc);
uint32_t ota_crc32_compute(const void *data, uint32_t len);

#endif
```

Create `Ota/ota_crc32.c`:

```c
#include "ota_crc32.h"

#include <stdint.h>

uint32_t ota_crc32_begin(void)
{
        return 0xFFFFFFFFUL;
}

uint32_t ota_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
        const uint8_t *bytes = (const uint8_t *)data;

        if (bytes == 0)
                return crc;
        for (uint32_t i = 0; i < len; i++) {
                crc ^= bytes[i];
                for (uint8_t bit = 0; bit < 8U; bit++) {
                        if ((crc & 1UL) != 0UL)
                                crc = (crc >> 1U) ^ 0xEDB88320UL;
                        else
                                crc >>= 1U;
                }
        }
        return crc;
}

uint32_t ota_crc32_finish(uint32_t crc)
{
        return crc ^ 0xFFFFFFFFUL;
}

uint32_t ota_crc32_compute(const void *data, uint32_t len)
{
        return ota_crc32_finish(ota_crc32_update(ota_crc32_begin(), data, len));
}
```

- [ ] **Step 5: Run the guard and verify it passes**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_layout_crc_guard_check.ps1
```

Expected: PASS with `OTA layout and CRC guard OK`.

- [ ] **Step 6: Commit**

```bash
git add tests/ota_layout_crc_guard_check.ps1 Ota/ota_layout.h Ota/ota_crc32.h Ota/ota_crc32.c
git commit -m "feat: add OTA layout and crc primitives"
```

---

### Task 2: OTA Manifest Format

**Files:**
- Create: `tests/ota_manifest_guard_check.ps1`
- Create: `Ota/ota_manifest.h`
- Create: `Ota/ota_manifest.c`

- [ ] **Step 1: Write the failing manifest guard**

Create `tests/ota_manifest_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "Ota\ota_manifest.h"
$sourcePath = Join-Path $root "Ota\ota_manifest.c"

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

Assert-Contains $header "#define\s+OTA_MANIFEST_MAGIC\s+0x3141544FUL" "manifest magic must be OTA1"
Assert-Contains $header "OTA_MANIFEST_STATE_IDLE\s*=\s*0" "idle state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_RECEIVING\s*=\s*1" "receiving state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_STAGED\s*=\s*2" "staged state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_PENDING\s*=\s*3" "pending state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_APPLIED\s*=\s*4" "applied state missing"
Assert-Contains $header "OTA_MANIFEST_STATE_ERROR\s*=\s*5" "error state missing"
Assert-Contains $header "uint32_t\s+image_size" "image size field missing"
Assert-Contains $header "uint32_t\s+image_crc32" "image crc field missing"
Assert-Contains $header "uint32_t\s+received_size" "received size field missing"
Assert-Contains $header "uint32_t\s+target_app_addr" "target app addr field missing"
Assert-Contains $header "uint32_t\s+target_app_max_size" "target max size field missing"
Assert-Contains $header "uint32_t\s+manifest_crc32" "manifest crc field missing"

Assert-Contains $source "ota_manifest_crc" "manifest crc implementation missing"
Assert-Contains $source "manifest_crc32\s*=\s*0" "manifest crc must zero its own field before calculation"
Assert-Contains $source "OTA_APP_BASE" "manifest validation must check app base"
Assert-Contains $source "OTA_APP_SIZE" "manifest validation must check app size"

Write-Output "OTA manifest guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_manifest_guard_check.ps1
```

Expected: FAIL with `Missing required file: ...\Ota\ota_manifest.h`.

- [ ] **Step 3: Add the manifest API**

Create `Ota/ota_manifest.h`:

```c
#ifndef OTA_MANIFEST_H
#define OTA_MANIFEST_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_MANIFEST_MAGIC   0x3141544FUL
#define OTA_MANIFEST_VERSION 1U

typedef enum {
        OTA_MANIFEST_STATE_IDLE = 0,
        OTA_MANIFEST_STATE_RECEIVING = 1,
        OTA_MANIFEST_STATE_STAGED = 2,
        OTA_MANIFEST_STATE_PENDING = 3,
        OTA_MANIFEST_STATE_APPLIED = 4,
        OTA_MANIFEST_STATE_ERROR = 5,
} ota_manifest_state_t;

typedef struct {
        uint32_t magic;
        uint16_t version;
        uint16_t header_size;
        uint32_t state;
        uint32_t image_size;
        uint32_t image_crc32;
        uint32_t received_size;
        uint32_t target_app_addr;
        uint32_t target_app_max_size;
        uint32_t sequence;
        uint32_t manifest_crc32;
} ota_manifest_t;

void ota_manifest_init(ota_manifest_t *manifest);
uint32_t ota_manifest_crc(const ota_manifest_t *manifest);
void ota_manifest_finalize(ota_manifest_t *manifest);
bool ota_manifest_is_valid(const ota_manifest_t *manifest);
bool ota_manifest_is_pending(const ota_manifest_t *manifest);
void ota_manifest_set_state(ota_manifest_t *manifest, ota_manifest_state_t state);

#endif
```

- [ ] **Step 4: Add manifest implementation**

Create `Ota/ota_manifest.c`:

```c
#include "ota_manifest.h"

#include "ota_crc32.h"
#include "ota_layout.h"
#include <string.h>

void ota_manifest_init(ota_manifest_t *manifest)
{
        if (manifest == 0)
                return;
        memset(manifest, 0, sizeof(*manifest));
        manifest->magic = OTA_MANIFEST_MAGIC;
        manifest->version = OTA_MANIFEST_VERSION;
        manifest->header_size = (uint16_t)sizeof(*manifest);
        manifest->state = OTA_MANIFEST_STATE_IDLE;
        manifest->target_app_addr = OTA_APP_BASE;
        manifest->target_app_max_size = OTA_APP_SIZE;
        ota_manifest_finalize(manifest);
}

uint32_t ota_manifest_crc(const ota_manifest_t *manifest)
{
        ota_manifest_t copy;

        if (manifest == 0)
                return 0UL;
        copy = *manifest;
        copy.manifest_crc32 = 0UL;
        return ota_crc32_compute(&copy, (uint32_t)sizeof(copy));
}

void ota_manifest_finalize(ota_manifest_t *manifest)
{
        if (manifest == 0)
                return;
        manifest->manifest_crc32 = 0UL;
        manifest->manifest_crc32 = ota_manifest_crc(manifest);
}

bool ota_manifest_is_valid(const ota_manifest_t *manifest)
{
        if (manifest == 0)
                return false;
        if (manifest->magic != OTA_MANIFEST_MAGIC)
                return false;
        if (manifest->version != OTA_MANIFEST_VERSION)
                return false;
        if (manifest->header_size != (uint16_t)sizeof(*manifest))
                return false;
        if (manifest->target_app_addr != OTA_APP_BASE)
                return false;
        if (manifest->target_app_max_size != OTA_APP_SIZE)
                return false;
        if (manifest->image_size > OTA_APP_SIZE)
                return false;
        if (manifest->received_size > manifest->image_size)
                return false;
        if (manifest->manifest_crc32 != ota_manifest_crc(manifest))
                return false;
        return true;
}

bool ota_manifest_is_pending(const ota_manifest_t *manifest)
{
        return ota_manifest_is_valid(manifest) &&
               manifest->state == OTA_MANIFEST_STATE_PENDING &&
               manifest->image_size > 0UL &&
               manifest->received_size == manifest->image_size;
}

void ota_manifest_set_state(ota_manifest_t *manifest, ota_manifest_state_t state)
{
        if (manifest == 0)
                return;
        manifest->state = (uint32_t)state;
        ota_manifest_finalize(manifest);
}
```

- [ ] **Step 5: Run the guard and verify it passes**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_manifest_guard_check.ps1
```

Expected: PASS with `OTA manifest guard OK`.

- [ ] **Step 6: Commit**

```bash
git add tests/ota_manifest_guard_check.ps1 Ota/ota_manifest.h Ota/ota_manifest.c
git commit -m "feat: define OTA manifest format"
```

---

### Task 3: Build Environments, Linker Scripts, And App Vector Offset

**Files:**
- Create: `tests/ota_partition_guard_check.ps1`
- Create: `STM32F103XX_APP.ld`
- Create: `STM32F103XX_BOOTLOADER.ld`
- Modify: `platformio.ini`
- Modify: `Core/Src/system_stm32f1xx.c`

- [ ] **Step 1: Write the failing partition guard**

Create `tests/ota_partition_guard_check.ps1`:

```powershell
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
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_partition_guard_check.ps1
```

Expected: FAIL with `Missing required file: ...\STM32F103XX_APP.ld`.

- [ ] **Step 3: Add the app linker script**

Copy the existing `STM32F103XX_FLASH.ld` to `STM32F103XX_APP.ld`, then change only the `FLASH` memory line:

```ld
MEMORY
{
RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 48K
FLASH (rx)      : ORIGIN = 0x8008000, LENGTH = 224K
}
```

- [ ] **Step 4: Add the bootloader linker script**

Copy the existing `STM32F103XX_FLASH.ld` to `STM32F103XX_BOOTLOADER.ld`, then change only the `FLASH` memory line:

```ld
MEMORY
{
RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 48K
FLASH (rx)      : ORIGIN = 0x8000000, LENGTH = 32K
}
```

- [ ] **Step 5: Allow vector offset override**

In `Core/Src/system_stm32f1xx.c`, replace the unconditional Flash vector definitions inside `#if defined(USER_VECT_TAB_ADDRESS)` with this form:

```c
#if defined(VECT_TAB_SRAM)
#ifndef VECT_TAB_BASE_ADDRESS
#define VECT_TAB_BASE_ADDRESS   SRAM_BASE
#endif
#ifndef VECT_TAB_OFFSET
#define VECT_TAB_OFFSET         0x00000000U
#endif
#else
#ifndef VECT_TAB_BASE_ADDRESS
#define VECT_TAB_BASE_ADDRESS   FLASH_BASE
#endif
#ifndef VECT_TAB_OFFSET
#define VECT_TAB_OFFSET         0x00000000U
#endif
#endif
```

- [ ] **Step 6: Add app and bootloader PlatformIO envs**

Append these envs to `platformio.ini`:

```ini
[env:app]
extends = env:genericSTM32F103RC
board_build.ldscript = STM32F103XX_APP.ld
build_flags =
        ${env:genericSTM32F103RC.build_flags}
        -DUSER_VECT_TAB_ADDRESS
        -DVECT_TAB_OFFSET=0x00008000U
build_src_filter =
        ${env:genericSTM32F103RC.build_src_filter}
        +<Ota/>

[env:bootloader]
platform = ststm32
board = genericSTM32F103RC
monitor_speed = 115200
platform_packages =
        toolchain-gccarmnoneeabi@1.140201.0
extra_scripts =
        add_newlibnano.py
build_flags =
        -D STM32F103xE
        -flto
        -fno-fat-lto-objects
        -Wl,-O1
        -DDEBUG_PRINT=0
        -I "Core/Inc"
        -I "Drivers/CMSIS/Include"
        -I "Drivers/CMSIS/Device/ST/STM32F1xx/Include"
        -I "Drivers/STM32F1xx_HAL_Driver/Inc"
        -I "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy"
        -I "Lib"
        -I "Lib/w25qxx"
        -I "Ota"
build_src_filter =
        +<startup_stm32f103xe.s>
        +<Bootloader/>
        +<Ota/>
        +<Core/Src/gpio.c>
        +<Core/Src/spi.c>
        +<Core/Src/stm32f1xx_hal_msp.c>
        +<Core/Src/system_stm32f1xx.c>
        +<Core/Src/syscalls.c>
        +<Core/Src/sysmem.c>
        +<Drivers/>
        +<Lib/w25qxx/>
board_build.ldscript = STM32F103XX_BOOTLOADER.ld
debug_tool = stlink
```

- [ ] **Step 7: Run the partition guard and verify it passes**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_partition_guard_check.ps1
```

Expected: PASS with `OTA partition guard OK`.

- [ ] **Step 8: Verify the existing default build still passes**

Run:

```bash
pio run -e genericSTM32F103RC
```

Expected: SUCCESS. This confirms the old env still builds before the app env is switched on.

- [ ] **Step 9: Commit**

```bash
git add tests/ota_partition_guard_check.ps1 STM32F103XX_APP.ld STM32F103XX_BOOTLOADER.ld platformio.ini Core/Src/system_stm32f1xx.c
git commit -m "build: add OTA app and bootloader partitions"
```

---

### Task 4: Move LittleFS And Migrate Known User Data

**Files:**
- Create: `tests/ota_littlefs_guard_check.ps1`
- Modify: `Lib/littlefs/lfs_port.c`
- Modify: `Lib/littlefs/lfs_port.h`

- [ ] **Step 1: Write the failing LittleFS guard**

Create `tests/ota_littlefs_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourcePath = Join-Path $root "Lib\littlefs\lfs_port.c"
$headerPath = Join-Path $root "Lib\littlefs\lfs_port.h"

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

function Assert-NotContains($Text, $Pattern, $Message) {
    if ($Text -match $Pattern) {
        throw $Message
    }
}

$source = Read-Text $sourcePath
$header = Read-Text $headerPath

Assert-Contains $source '#include "ota_layout.h"' "lfs port must include OTA layout"
Assert-Contains $source "s_lfs_offset_blocks" "lfs callbacks must use a runtime offset for migration"
Assert-Contains $source "OTA_LFS_OFFSET_BLOCKS" "new lfs offset must come from OTA layout"
Assert-Contains $source "OTA_OLD_LFS_OFFSET_BLOCKS" "old lfs offset must remain readable for migration"
Assert-Contains $source "\.block_count\s*=\s*OTA_LFS_BLOCK_COUNT" "new lfs block count must fit W25Q16 after OTA slot"
Assert-Contains $source "battery_hist" "battery history must be migrated"
Assert-Contains $source "uptime_ckpt" "uptime checkpoint must be migrated"
Assert-Contains $source "boot_count" "boot count must be migrated"
Assert-Contains $source "lfs_migrate_from_old_offset" "migration function missing"
Assert-Contains $header "int\s+lfs_migrate_from_old_offset\s*\(void\)" "migration prototype missing"
Assert-NotContains $source "#define\s+OFFSETBLOCK\s+3" "fixed old offset macro must be removed"

Write-Output "OTA LittleFS guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_littlefs_guard_check.ps1
```

Expected: FAIL because `lfs_port.c` does not include `ota_layout.h`.

- [ ] **Step 3: Replace the fixed LittleFS offset with a runtime offset**

In `Lib/littlefs/lfs_port.c`, add the include and replace `OFFSETBLOCK` math:

```c
#include "ota_layout.h"

static uint32_t s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;

static uint32_t lfs_block_addr(const struct lfs_config *c, lfs_block_t block, lfs_off_t off)
{
        return (uint32_t)((s_lfs_offset_blocks + (uint32_t)block) * c->block_size + off);
}
```

Change the callbacks to use the helper:

```c
if (W25Qx_OK == w25qxx_read((uint8_t *)buffer, lfs_block_addr(c, block, off), size))
```

```c
if (W25Qx_OK == w25qxx_write((uint8_t *)buffer, lfs_block_addr(c, block, off), size))
```

```c
if (W25Qx_OK == w25qxx_erase_block(lfs_block_addr(c, block, 0)))
```

Change `lfs_w25qxx_cfg.block_count`:

```c
.block_count = OTA_LFS_BLOCK_COUNT,
```

- [ ] **Step 4: Add old-offset migration helpers**

Add one 4 KB migration buffer and copy the known files one by one:

```c
static uint8_t s_lfs_migrate_buffer[LFS_PORT_FILE_MAX];
static int s_lfs_migration_checked;

static const char *const s_lfs_migrate_files[] = {
        "boot_count",
        "battery_hist",
        "uptime_ckpt",
};

static int lfs_mount_raw_at(uint32_t offset_blocks)
{
        s_lfs_offset_blocks = offset_blocks;
        return lfs_mount(&s_lfs, &lfs_w25qxx_cfg);
}

static void lfs_force_unmount_raw(void)
{
        (void)lfs_unmount(&s_lfs);
        s_mounted = 0;
        s_ready = 0;
}
```

Add the migration function:

```c
int lfs_migrate_from_old_offset(void)
{
        if (s_lfs_migration_checked)
                return 0;
        s_lfs_migration_checked = 1;

        s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
        if (lfs_mount_raw_at(OTA_LFS_OFFSET_BLOCKS) == 0) {
                lfs_force_unmount_raw();
                return 0;
        }
        lfs_force_unmount_raw();

        if (lfs_mount_raw_at(OTA_OLD_LFS_OFFSET_BLOCKS) != 0) {
                lfs_force_unmount_raw();
                s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
                return 0;
        }

        for (unsigned int i = 0; i < sizeof(s_lfs_migrate_files) / sizeof(s_lfs_migrate_files[0]); i++) {
                lfs_file_t file;
                lfs_ssize_t read_len;

                if (lfs_file_open(&s_lfs, &file, s_lfs_migrate_files[i], LFS_O_RDONLY) < 0)
                        continue;
                read_len = lfs_file_read(&s_lfs, &file, s_lfs_migrate_buffer,
                                         sizeof(s_lfs_migrate_buffer));
                (void)lfs_file_close(&s_lfs, &file);
                lfs_force_unmount_raw();

                s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
                if (lfs_mount_raw_at(OTA_LFS_OFFSET_BLOCKS) != 0) {
                        (void)lfs_format(&s_lfs, &lfs_w25qxx_cfg);
                        (void)lfs_mount_raw_at(OTA_LFS_OFFSET_BLOCKS);
                }
                if (read_len > 0) {
                        lfs_file_t out;
                        if (lfs_file_open(&s_lfs, &out, s_lfs_migrate_files[i],
                                          LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == 0) {
                                (void)lfs_file_write(&s_lfs, &out, s_lfs_migrate_buffer,
                                                     (lfs_size_t)read_len);
                                (void)lfs_file_close(&s_lfs, &out);
                        }
                }
                lfs_force_unmount_raw();
                (void)lfs_mount_raw_at(OTA_OLD_LFS_OFFSET_BLOCKS);
        }

        lfs_force_unmount_raw();
        s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
        return 0;
}
```

Before the first normal mount in `lfs_first_run()`, call:

```c
(void)lfs_migrate_from_old_offset();
```

- [ ] **Step 5: Add the header prototype**

In `Lib/littlefs/lfs_port.h`, add:

```c
int lfs_migrate_from_old_offset(void);
```

- [ ] **Step 6: Run the guard and build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_littlefs_guard_check.ps1
pio run -e genericSTM32F103RC
```

Expected: guard PASS and PlatformIO SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add tests/ota_littlefs_guard_check.ps1 Lib/littlefs/lfs_port.c Lib/littlefs/lfs_port.h
git commit -m "feat: reserve OTA slot before LittleFS"
```

---

### Task 5: Shared W25Q OTA Store

**Files:**
- Create: `tests/ota_store_guard_check.ps1`
- Create: `Ota/ota_store.h`
- Create: `Ota/ota_store.c`

- [ ] **Step 1: Write the failing store guard**

Create `tests/ota_store_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "Ota\ota_store.h"
$sourcePath = Join-Path $root "Ota\ota_store.c"

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

Assert-Contains $header "ota_store_read_manifest" "read manifest prototype missing"
Assert-Contains $header "ota_store_write_manifest" "write manifest prototype missing"
Assert-Contains $header "ota_store_erase_image_slot" "erase image slot prototype missing"
Assert-Contains $header "ota_store_write_image" "write image prototype missing"
Assert-Contains $header "ota_store_read_image" "read image prototype missing"
Assert-Contains $source "OTA_MANIFEST_ADDR" "manifest address constant must be used"
Assert-Contains $source "OTA_IMAGE_ADDR" "image address constant must be used"
Assert-Contains $source "OTA_IMAGE_SLOT_SIZE" "image slot size must be checked"
Assert-Contains $source "w25qxx_erase_block" "store must erase W25Q sectors"
Assert-Contains $source "w25qxx_write" "store must write W25Q"
Assert-Contains $source "w25qxx_read" "store must read W25Q"

Write-Output "OTA store guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_store_guard_check.ps1
```

Expected: FAIL with missing `Ota\ota_store.h`.

- [ ] **Step 3: Add the store API**

Create `Ota/ota_store.h`:

```c
#ifndef OTA_STORE_H
#define OTA_STORE_H

#include "ota_manifest.h"
#include <stdbool.h>
#include <stdint.h>

bool ota_store_read_manifest(ota_manifest_t *manifest);
bool ota_store_write_manifest(const ota_manifest_t *manifest);
bool ota_store_clear_manifest(void);
bool ota_store_erase_image_slot(uint32_t image_size);
bool ota_store_write_image(uint32_t offset, const uint8_t *data, uint32_t len);
bool ota_store_read_image(uint32_t offset, uint8_t *data, uint32_t len);

#endif
```

- [ ] **Step 4: Add the store implementation**

Create `Ota/ota_store.c`:

```c
#include "ota_store.h"

#include "ota_layout.h"
#include "w25qxx.h"
#include <string.h>

static bool ota_store_range_ok(uint32_t offset, uint32_t len)
{
        if (offset > OTA_IMAGE_SLOT_SIZE)
                return false;
        if (len > OTA_IMAGE_SLOT_SIZE - offset)
                return false;
        return true;
}

bool ota_store_read_manifest(ota_manifest_t *manifest)
{
        if (manifest == 0)
                return false;
        return w25qxx_read((uint8_t *)manifest, OTA_MANIFEST_ADDR,
                           (uint32_t)sizeof(*manifest)) == W25Qx_OK;
}

bool ota_store_write_manifest(const ota_manifest_t *manifest)
{
        ota_manifest_t copy;

        if (manifest == 0)
                return false;
        copy = *manifest;
        ota_manifest_finalize(&copy);
        if (w25qxx_erase_block(OTA_MANIFEST_ADDR) != W25Qx_OK)
                return false;
        return w25qxx_write((uint8_t *)&copy, OTA_MANIFEST_ADDR,
                            (uint32_t)sizeof(copy)) == W25Qx_OK;
}

bool ota_store_clear_manifest(void)
{
        return w25qxx_erase_block(OTA_MANIFEST_ADDR) == W25Qx_OK;
}

bool ota_store_erase_image_slot(uint32_t image_size)
{
        uint32_t erase_size;

        if (image_size == 0UL || image_size > OTA_IMAGE_SLOT_SIZE)
                return false;
        erase_size = (image_size + OTA_W25Q_SECTOR_SIZE - 1UL) &
                     ~(OTA_W25Q_SECTOR_SIZE - 1UL);
        for (uint32_t off = 0; off < erase_size; off += OTA_W25Q_SECTOR_SIZE) {
                if (w25qxx_erase_block(OTA_IMAGE_ADDR + off) != W25Qx_OK)
                        return false;
        }
        return true;
}

bool ota_store_write_image(uint32_t offset, const uint8_t *data, uint32_t len)
{
        if (data == 0 || !ota_store_range_ok(offset, len))
                return false;
        return w25qxx_write((uint8_t *)data, OTA_IMAGE_ADDR + offset, len) == W25Qx_OK;
}

bool ota_store_read_image(uint32_t offset, uint8_t *data, uint32_t len)
{
        if (data == 0 || !ota_store_range_ok(offset, len))
                return false;
        return w25qxx_read(data, OTA_IMAGE_ADDR + offset, len) == W25Qx_OK;
}
```

- [ ] **Step 5: Run guards and app compatibility build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_store_guard_check.ps1
pio run -e genericSTM32F103RC
```

Expected: guard PASS and PlatformIO SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add tests/ota_store_guard_check.ps1 Ota/ota_store.h Ota/ota_store.c
git commit -m "feat: add OTA W25Q store helpers"
```

---

### Task 6: App USART1 OTA Command Handling

**Files:**
- Create: `tests/ota_uart_guard_check.ps1`
- Create: `App/ota_update.h`
- Create: `App/ota_update.c`
- Modify: `App/uart_forward.c`

- [ ] **Step 1: Write the failing UART OTA guard**

Create `tests/ota_uart_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$headerPath = Join-Path $root "App\ota_update.h"
$sourcePath = Join-Path $root "App\ota_update.c"
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

$header = Read-Text $headerPath
$source = Read-Text $sourcePath
$uart = Read-Text $uartPath

Assert-Contains $header "typedef\s+void\s+\(\*ota_reply_fn\)" "OTA reply callback type missing"
Assert-Contains $header "bool\s+ota_command_process\s*\(" "OTA command process prototype missing"
Assert-Contains $source "OTA BEGIN" "OTA BEGIN handler missing"
Assert-Contains $source "OTA DATA" "OTA DATA handler missing"
Assert-Contains $source "OTA END" "OTA END handler missing"
Assert-Contains $source "OTA APPLY" "OTA APPLY handler missing"
Assert-Contains $source "OTA ABORT" "OTA ABORT handler missing"
Assert-Contains $source "OTA STATUS\?" "OTA STATUS handler missing"
Assert-Contains $source "OTA_TRANSFER_MAX_DATA_LEN" "OTA data block length must be bounded"
Assert-Contains $source "HAL_NVIC_SystemReset" "OTA APPLY must reset the MCU"
Assert-Contains $source "ota_store_write_image" "OTA DATA must write to image slot"
Assert-Contains $source "ota_store_write_manifest" "OTA must write manifest state"
Assert-Contains $source "ota_crc32_update" "OTA must calculate CRC32"
Assert-Contains $uart '#include "ota_update.h"' "uart_forward must include OTA update module"
Assert-Contains $uart 'strncmp\(line,\s*"OTA ",\s*4\)' "uart_forward must route OTA commands"
Assert-Contains $uart "ota_command_process" "uart_forward must call ota_command_process"

Write-Output "OTA UART guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_uart_guard_check.ps1
```

Expected: FAIL with missing `App\ota_update.h`.

- [ ] **Step 3: Add OTA update API**

Create `App/ota_update.h`:

```c
#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>

typedef void (*ota_reply_fn)(const char *text, void *ctx);

bool ota_command_process(char *line, ota_reply_fn reply, void *ctx);

#endif
```

- [ ] **Step 4: Add OTA update implementation**

Create `App/ota_update.c` with this structure:

```c
#include "ota_update.h"

#include "main.h"
#include "ota_crc32.h"
#include "ota_layout.h"
#include "ota_manifest.h"
#include "ota_store.h"
#include "storage.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
        bool active;
        bool staged;
        ota_manifest_t manifest;
        uint32_t running_crc;
} ota_rx_state_t;

static ota_rx_state_t s_ota;
static uint8_t s_ota_chunk[OTA_TRANSFER_MAX_DATA_LEN];

static void ota_reply(ota_reply_fn reply, void *ctx, const char *text)
{
        if (reply != 0)
                reply(text, ctx);
}
```

Add helpers for decimal, hex byte, and `key=value` parsing:

```c
static bool ota_parse_u32_dec(const char *s, uint32_t *out)
{
        uint32_t value = 0;
        bool has_digit = false;

        if (s == 0 || out == 0)
                return false;
        while (*s >= '0' && *s <= '9') {
                has_digit = true;
                value = value * 10UL + (uint32_t)(*s - '0');
                s++;
        }
        if (!has_digit || (*s != '\0' && *s != ' '))
                return false;
        *out = value;
        return true;
}

static int ota_hex_value(char ch)
{
        if (ch >= '0' && ch <= '9')
                return ch - '0';
        if (ch >= 'a' && ch <= 'f')
                return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
                return ch - 'A' + 10;
        return -1;
}

static bool ota_parse_u32_hex(const char *s, uint32_t *out)
{
        uint32_t value = 0;
        unsigned int digits = 0;

        if (s == 0 || out == 0)
                return false;
        while (*s != '\0' && *s != ' ') {
                int v = ota_hex_value(*s);
                if (v < 0 || digits >= 8U)
                        return false;
                value = (value << 4U) | (uint32_t)v;
                digits++;
                s++;
        }
        if (digits == 0U)
                return false;
        *out = value;
        return true;
}

static const char *ota_arg_value(char *line, const char *key)
{
        const size_t key_len = strlen(key);
        char *p = line;

        while (p != 0 && *p != '\0') {
                while (*p == ' ')
                        p++;
                if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
                        return &p[key_len + 1U];
                p = strchr(p, ' ');
                if (p == 0)
                        break;
                p++;
        }
        return 0;
}

static bool ota_decode_hex(const char *hex, uint8_t *out, uint32_t len)
{
        for (uint32_t i = 0; i < len; i++) {
                int hi = ota_hex_value(hex[i * 2U]);
                int lo = ota_hex_value(hex[i * 2U + 1U]);
                if (hi < 0 || lo < 0)
                        return false;
                out[i] = (uint8_t)((hi << 4) | lo);
        }
        return hex[len * 2U] == '\0' || hex[len * 2U] == ' ';
}
```

Add command handlers:

```c
static bool ota_begin(char *line, ota_reply_fn reply, void *ctx)
{
        uint32_t size;
        uint32_t crc;

        if (!ota_parse_u32_dec(ota_arg_value(line, "size"), &size) ||
            !ota_parse_u32_hex(ota_arg_value(line, "crc"), &crc) ||
            size == 0UL || size > OTA_APP_SIZE) {
                ota_reply(reply, ctx, "ERR OTA BEGIN");
                return true;
        }
        storage_flash_init();
        if (!storage_flash_is_present() || !ota_store_clear_manifest() ||
            !ota_store_erase_image_slot(size)) {
                ota_reply(reply, ctx, "ERR OTA FLASH");
                return true;
        }
        ota_manifest_init(&s_ota.manifest);
        s_ota.manifest.state = OTA_MANIFEST_STATE_RECEIVING;
        s_ota.manifest.image_size = size;
        s_ota.manifest.image_crc32 = crc;
        s_ota.manifest.received_size = 0UL;
        ota_manifest_finalize(&s_ota.manifest);
        if (!ota_store_write_manifest(&s_ota.manifest)) {
                ota_reply(reply, ctx, "ERR OTA MANIFEST");
                return true;
        }
        s_ota.active = true;
        s_ota.staged = false;
        s_ota.running_crc = ota_crc32_begin();
        ota_reply(reply, ctx, "OK OTA BEGIN");
        return true;
}
```

```c
static bool ota_data(char *line, ota_reply_fn reply, void *ctx)
{
        uint32_t off;
        uint32_t len;
        uint32_t crc;
        const char *hex;
        uint32_t chunk_crc;

        if (!s_ota.active ||
            !ota_parse_u32_dec(ota_arg_value(line, "off"), &off) ||
            !ota_parse_u32_dec(ota_arg_value(line, "len"), &len) ||
            !ota_parse_u32_hex(ota_arg_value(line, "crc"), &crc)) {
                ota_reply(reply, ctx, "ERR OTA DATA");
                return true;
        }
        hex = ota_arg_value(line, "hex");
        if (hex == 0 || len == 0UL || len > OTA_TRANSFER_MAX_DATA_LEN ||
            off != s_ota.manifest.received_size ||
            len > s_ota.manifest.image_size - off ||
            !ota_decode_hex(hex, s_ota_chunk, len)) {
                ota_reply(reply, ctx, "ERR OTA DATA");
                return true;
        }
        chunk_crc = ota_crc32_compute(s_ota_chunk, len);
        if (chunk_crc != crc) {
                ota_reply(reply, ctx, "ERR OTA CRC");
                return true;
        }
        if (!ota_store_write_image(off, s_ota_chunk, len)) {
                ota_reply(reply, ctx, "ERR OTA WRITE");
                return true;
        }
        s_ota.running_crc = ota_crc32_update(s_ota.running_crc, s_ota_chunk, len);
        s_ota.manifest.received_size += len;
        ota_manifest_finalize(&s_ota.manifest);
        (void)ota_store_write_manifest(&s_ota.manifest);
        ota_reply(reply, ctx, "OK OTA DATA");
        return true;
}
```

```c
static bool ota_end(ota_reply_fn reply, void *ctx)
{
        uint32_t final_crc;

        if (!s_ota.active || s_ota.manifest.received_size != s_ota.manifest.image_size) {
                ota_reply(reply, ctx, "ERR OTA END");
                return true;
        }
        final_crc = ota_crc32_finish(s_ota.running_crc);
        if (final_crc != s_ota.manifest.image_crc32) {
                ota_manifest_set_state(&s_ota.manifest, OTA_MANIFEST_STATE_ERROR);
                (void)ota_store_write_manifest(&s_ota.manifest);
                ota_reply(reply, ctx, "ERR OTA IMAGECRC");
                return true;
        }
        ota_manifest_set_state(&s_ota.manifest, OTA_MANIFEST_STATE_STAGED);
        if (!ota_store_write_manifest(&s_ota.manifest)) {
                ota_reply(reply, ctx, "ERR OTA MANIFEST");
                return true;
        }
        s_ota.staged = true;
        ota_reply(reply, ctx, "OK OTA END");
        return true;
}
```

```c
static bool ota_apply(ota_reply_fn reply, void *ctx)
{
        if (!s_ota.staged || !ota_manifest_is_valid(&s_ota.manifest)) {
                ota_reply(reply, ctx, "ERR OTA APPLY");
                return true;
        }
        ota_manifest_set_state(&s_ota.manifest, OTA_MANIFEST_STATE_PENDING);
        if (!ota_store_write_manifest(&s_ota.manifest)) {
                ota_reply(reply, ctx, "ERR OTA MANIFEST");
                return true;
        }
        ota_reply(reply, ctx, "OK OTA APPLY");
        HAL_NVIC_SystemReset();
        return true;
}
```

```c
bool ota_command_process(char *line, ota_reply_fn reply, void *ctx)
{
        char status[64];

        if (line == 0)
                return false;
        if (strncmp(line, "OTA BEGIN ", 10) == 0)
                return ota_begin(line, reply, ctx);
        if (strncmp(line, "OTA DATA ", 9) == 0)
                return ota_data(line, reply, ctx);
        if (strcmp(line, "OTA END") == 0)
                return ota_end(reply, ctx);
        if (strcmp(line, "OTA APPLY") == 0)
                return ota_apply(reply, ctx);
        if (strcmp(line, "OTA ABORT") == 0) {
                memset(&s_ota, 0, sizeof(s_ota));
                (void)ota_store_clear_manifest();
                ota_reply(reply, ctx, "OK OTA ABORT");
                return true;
        }
        if (strcmp(line, "OTA STATUS?") == 0) {
                (void)snprintf(status, sizeof(status), "OK OTA %s %lu/%lu",
                               s_ota.staged ? "staged" : (s_ota.active ? "receiving" : "idle"),
                               (unsigned long)s_ota.manifest.received_size,
                               (unsigned long)s_ota.manifest.image_size);
                ota_reply(reply, ctx, status);
                return true;
        }
        return false;
}
```

- [ ] **Step 5: Route OTA commands from USART1**

In `App/uart_forward.c`, add:

```c
#include "ota_update.h"
```

Add this helper near `uart_reply`:

```c
static void uart_ota_reply(const char *text, void *ctx)
{
        (void)ctx;
        uart_reply("%s\r\n", text);
}
```

At the start of `uart_process_command()` after empty-line handling, add:

```c
if (strncmp(line, "OTA ", 4) == 0) {
        if (!ota_command_process(line, uart_ota_reply, NULL))
                uart_reply("ERR OTA unknown\r\n");
        return;
}
```

Add `OTA` to the help line:

```c
uart_reply("OK CMDS OTA HELP TIME=HH:MM:SS GET TIME GET BAT GET ATT BATCAL=V BATGAIN=N BATOFF=N POSECAL POSESIGN=+1/-1\r\n");
```

- [ ] **Step 6: Run guard and app build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_uart_guard_check.ps1
pio run -e app
```

Expected: guard PASS and PlatformIO SUCCESS for the offset app build.

- [ ] **Step 7: Commit**

```bash
git add tests/ota_uart_guard_check.ps1 App/ota_update.h App/ota_update.c App/uart_forward.c
git commit -m "feat: receive OTA images over USART1"
```

---

### Task 7: Bootloader Skeleton And App Jump

**Files:**
- Create: `tests/ota_boot_jump_guard_check.ps1`
- Create: `Bootloader/boot_jump.h`
- Create: `Bootloader/boot_jump.c`
- Create: `Bootloader/main.c`

- [ ] **Step 1: Write the failing boot jump guard**

Create `tests/ota_boot_jump_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$jumpHeaderPath = Join-Path $root "Bootloader\boot_jump.h"
$jumpSourcePath = Join-Path $root "Bootloader\boot_jump.c"
$mainPath = Join-Path $root "Bootloader\main.c"

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

$jumpHeader = Read-Text $jumpHeaderPath
$jumpSource = Read-Text $jumpSourcePath
$main = Read-Text $mainPath

Assert-Contains $jumpHeader "bool\s+boot_app_is_valid\s*\(void\)" "app validity prototype missing"
Assert-Contains $jumpHeader "void\s+boot_jump_to_app\s*\(void\)" "jump prototype missing"
Assert-Contains $jumpSource "OTA_APP_BASE" "jump code must use OTA app base"
Assert-Contains $jumpSource "OTA_SRAM_BASE" "jump code must validate MSP in SRAM"
Assert-Contains $jumpSource "OTA_FLASH_END" "jump code must validate reset handler in Flash"
Assert-Contains $jumpSource "__set_MSP" "jump code must set MSP"
Assert-Contains $jumpSource "SCB->VTOR" "jump code must set vector table"
Assert-Contains $main "boot_jump_to_app" "bootloader main must jump to app"
Assert-Contains $main "MX_GPIO_Init" "bootloader must init GPIO before W25Q CS"
Assert-Contains $main "MX_SPI1_Init" "bootloader must init SPI1"

Write-Output "OTA boot jump guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_boot_jump_guard_check.ps1
```

Expected: FAIL with missing `Bootloader\boot_jump.h`.

- [ ] **Step 3: Add app validation and jump code**

Create `Bootloader/boot_jump.h`:

```c
#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdbool.h>

bool boot_app_is_valid(void);
void boot_jump_to_app(void);

#endif
```

Create `Bootloader/boot_jump.c`:

```c
#include "boot_jump.h"

#include "ota_layout.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef void (*boot_app_entry_t)(void);

bool boot_app_is_valid(void)
{
        const uint32_t msp = *(volatile uint32_t *)OTA_APP_BASE;
        const uint32_t reset = *(volatile uint32_t *)(OTA_APP_BASE + 4UL);

        if (msp < OTA_SRAM_BASE || msp > OTA_SRAM_END)
                return false;
        if (reset < OTA_APP_BASE || reset >= OTA_FLASH_END)
                return false;
        if ((reset & 1UL) == 0UL)
                return false;
        return true;
}

void boot_jump_to_app(void)
{
        const uint32_t app_msp = *(volatile uint32_t *)OTA_APP_BASE;
        const uint32_t app_reset = *(volatile uint32_t *)(OTA_APP_BASE + 4UL);
        const boot_app_entry_t app_entry = (boot_app_entry_t)app_reset;

        __disable_irq();
        HAL_RCC_DeInit();
        HAL_DeInit();
        SysTick->CTRL = 0;
        SysTick->LOAD = 0;
        SysTick->VAL = 0;
        SCB->VTOR = OTA_APP_BASE;
        __set_MSP(app_msp);
        __enable_irq();
        app_entry();
}
```

- [ ] **Step 4: Add minimal bootloader main**

Create `Bootloader/main.c`:

```c
#include "boot_jump.h"
#include "gpio.h"
#include "spi.h"
#include "stm32f1xx_hal.h"
#include "w25qxx.h"

static void BootClock_Config(void)
{
        RCC_OscInitTypeDef osc = {0};
        RCC_ClkInitTypeDef clk = {0};

        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState = RCC_PLL_NONE;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK)
                while (1) {}

        clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
        clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
        clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
        clk.APB1CLKDivider = RCC_HCLK_DIV1;
        clk.APB2CLKDivider = RCC_HCLK_DIV1;
        if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK)
                while (1) {}
}

int main(void)
{
        HAL_Init();
        BootClock_Config();
        MX_GPIO_Init();
        MX_SPI1_Init();
        w25qxx_reset();

        if (boot_app_is_valid())
                boot_jump_to_app();

        while (1) {
                HAL_Delay(250);
        }
}

void Error_Handler(void)
{
        while (1) {}
}
```

- [ ] **Step 5: Run guard and bootloader build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_boot_jump_guard_check.ps1
pio run -e bootloader
```

Expected: guard PASS and PlatformIO SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add tests/ota_boot_jump_guard_check.ps1 Bootloader/boot_jump.h Bootloader/boot_jump.c Bootloader/main.c
git commit -m "feat: add OTA bootloader app jump"
```

---

### Task 8: Bootloader OTA Install Flow

**Files:**
- Create: `tests/ota_boot_install_guard_check.ps1`
- Create: `Bootloader/boot_flash.h`
- Create: `Bootloader/boot_flash.c`
- Create: `Bootloader/boot_ota.h`
- Create: `Bootloader/boot_ota.c`
- Modify: `Bootloader/main.c`

- [ ] **Step 1: Write the failing boot install guard**

Create `tests/ota_boot_install_guard_check.ps1`:

```powershell
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$flashHeaderPath = Join-Path $root "Bootloader\boot_flash.h"
$flashSourcePath = Join-Path $root "Bootloader\boot_flash.c"
$otaHeaderPath = Join-Path $root "Bootloader\boot_ota.h"
$otaSourcePath = Join-Path $root "Bootloader\boot_ota.c"
$mainPath = Join-Path $root "Bootloader\main.c"

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

$flashHeader = Read-Text $flashHeaderPath
$flashSource = Read-Text $flashSourcePath
$otaHeader = Read-Text $otaHeaderPath
$otaSource = Read-Text $otaSourcePath
$main = Read-Text $mainPath

Assert-Contains $flashHeader "boot_flash_erase_app" "erase app prototype missing"
Assert-Contains $flashHeader "boot_flash_program" "program prototype missing"
Assert-Contains $flashSource "HAL_FLASHEx_Erase" "bootloader must erase app pages"
Assert-Contains $flashSource "HAL_FLASH_Program" "bootloader must program internal flash"
Assert-Contains $flashSource "FLASH_TYPEPROGRAM_HALFWORD" "STM32F1 must program halfwords"
Assert-Contains $otaHeader "boot_ota_apply_if_pending" "OTA apply prototype missing"
Assert-Contains $otaSource "ota_manifest_is_pending" "bootloader must only apply pending manifest"
Assert-Contains $otaSource "ota_store_read_image" "bootloader must read image slot"
Assert-Contains $otaSource "ota_crc32_update" "bootloader must verify image CRC"
Assert-Contains $otaSource "boot_flash_erase_app" "bootloader must erase app before programming"
Assert-Contains $otaSource "boot_flash_program" "bootloader must program app flash"
Assert-Contains $otaSource "OTA_MANIFEST_STATE_APPLIED" "bootloader must mark successful apply"
Assert-Contains $otaSource "OTA_MANIFEST_STATE_ERROR" "bootloader must mark failed apply"
Assert-Contains $main "boot_ota_apply_if_pending" "bootloader main must invoke OTA apply before app jump"

Write-Output "OTA boot install guard OK"
```

- [ ] **Step 2: Run the guard and verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_boot_install_guard_check.ps1
```

Expected: FAIL with missing `Bootloader\boot_flash.h`.

- [ ] **Step 3: Add internal Flash helpers**

Create `Bootloader/boot_flash.h`:

```c
#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

bool boot_flash_erase_app(uint32_t image_size);
bool boot_flash_program(uint32_t address, const uint8_t *data, uint32_t len);

#endif
```

Create `Bootloader/boot_flash.c`:

```c
#include "boot_flash.h"

#include "ota_layout.h"
#include "stm32f1xx_hal.h"

bool boot_flash_erase_app(uint32_t image_size)
{
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t page_error = 0;
        uint32_t pages;
        HAL_StatusTypeDef status;

        if (image_size == 0UL || image_size > OTA_APP_SIZE)
                return false;
        pages = (image_size + FLASH_PAGE_SIZE - 1UL) / FLASH_PAGE_SIZE;

        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = OTA_APP_BASE;
        erase.NbPages = pages;

        if (HAL_FLASH_Unlock() != HAL_OK)
                return false;
        status = HAL_FLASHEx_Erase(&erase, &page_error);
        (void)HAL_FLASH_Lock();
        return status == HAL_OK && page_error == 0xFFFFFFFFUL;
}

bool boot_flash_program(uint32_t address, const uint8_t *data, uint32_t len)
{
        HAL_StatusTypeDef status = HAL_OK;

        if (data == 0 || address < OTA_APP_BASE || address + len > OTA_FLASH_END)
                return false;
        if (HAL_FLASH_Unlock() != HAL_OK)
                return false;

        for (uint32_t i = 0; i < len; i += 2UL) {
                uint16_t halfword = data[i];
                if (i + 1UL < len)
                        halfword |= ((uint16_t)data[i + 1UL] << 8U);
                else
                        halfword |= 0xFF00U;
                status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                           address + i, halfword);
                if (status != HAL_OK)
                        break;
        }
        (void)HAL_FLASH_Lock();
        return status == HAL_OK;
}
```

- [ ] **Step 4: Add OTA apply flow**

Create `Bootloader/boot_ota.h`:

```c
#ifndef BOOT_OTA_H
#define BOOT_OTA_H

#include <stdbool.h>

bool boot_ota_apply_if_pending(void);

#endif
```

Create `Bootloader/boot_ota.c`:

```c
#include "boot_ota.h"

#include "boot_flash.h"
#include "ota_crc32.h"
#include "ota_layout.h"
#include "ota_manifest.h"
#include "ota_store.h"
#include <stdint.h>

#define BOOT_OTA_CHUNK_SIZE 256U

static uint8_t s_boot_ota_buf[BOOT_OTA_CHUNK_SIZE];

static void boot_ota_mark(ota_manifest_t *manifest, ota_manifest_state_t state)
{
        ota_manifest_set_state(manifest, state);
        (void)ota_store_write_manifest(manifest);
}

static bool boot_ota_image_crc_ok(const ota_manifest_t *manifest)
{
        uint32_t crc = ota_crc32_begin();
        uint32_t remaining = manifest->image_size;
        uint32_t offset = 0;

        while (remaining > 0UL) {
                uint32_t chunk = remaining > BOOT_OTA_CHUNK_SIZE ? BOOT_OTA_CHUNK_SIZE : remaining;
                if (!ota_store_read_image(offset, s_boot_ota_buf, chunk))
                        return false;
                crc = ota_crc32_update(crc, s_boot_ota_buf, chunk);
                offset += chunk;
                remaining -= chunk;
        }
        return ota_crc32_finish(crc) == manifest->image_crc32;
}

static bool boot_ota_program_app(const ota_manifest_t *manifest)
{
        uint32_t remaining = manifest->image_size;
        uint32_t offset = 0;

        if (!boot_flash_erase_app(manifest->image_size))
                return false;
        while (remaining > 0UL) {
                uint32_t chunk = remaining > BOOT_OTA_CHUNK_SIZE ? BOOT_OTA_CHUNK_SIZE : remaining;
                if (!ota_store_read_image(offset, s_boot_ota_buf, chunk))
                        return false;
                if (!boot_flash_program(OTA_APP_BASE + offset, s_boot_ota_buf, chunk))
                        return false;
                offset += chunk;
                remaining -= chunk;
        }
        return true;
}

bool boot_ota_apply_if_pending(void)
{
        ota_manifest_t manifest;

        if (!ota_store_read_manifest(&manifest))
                return false;
        if (!ota_manifest_is_pending(&manifest))
                return false;
        if (!boot_ota_image_crc_ok(&manifest)) {
                boot_ota_mark(&manifest, OTA_MANIFEST_STATE_ERROR);
                return false;
        }
        if (!boot_ota_program_app(&manifest)) {
                boot_ota_mark(&manifest, OTA_MANIFEST_STATE_ERROR);
                return false;
        }
        if (!boot_ota_image_crc_ok(&manifest)) {
                boot_ota_mark(&manifest, OTA_MANIFEST_STATE_ERROR);
                return false;
        }
        boot_ota_mark(&manifest, OTA_MANIFEST_STATE_APPLIED);
        return true;
}
```

- [ ] **Step 5: Call OTA apply before app jump**

Modify `Bootloader/main.c`:

```c
#include "boot_ota.h"
```

After `w25qxx_reset();`, add:

```c
(void)boot_ota_apply_if_pending();
```

- [ ] **Step 6: Run guard and bootloader build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\ota_boot_install_guard_check.ps1
pio run -e bootloader
```

Expected: guard PASS and PlatformIO SUCCESS. If bootloader exceeds 32 KB, reduce included sources before changing the partition size.

- [ ] **Step 7: Commit**

```bash
git add tests/ota_boot_install_guard_check.ps1 Bootloader/boot_flash.h Bootloader/boot_flash.c Bootloader/boot_ota.h Bootloader/boot_ota.c Bootloader/main.c
git commit -m "feat: install pending OTA images in bootloader"
```

---

### Task 9: Final Integration Verification

**Files:**
- Modify: `README.md`
- Use existing: `tests/*.ps1`
- Use existing: `platformio.ini`

- [ ] **Step 1: Add README build and OTA notes**

In `README.md`, add this build section near the existing build commands:

````markdown
## OTA Builds

The two-stage OTA build uses separate PlatformIO environments:

```bash
pio run -e bootloader
pio run -e app
```

Flash the bootloader at `0x08000000`. The app image is linked for `0x08008000`.
The USART1 text OTA protocol accepts `OTA BEGIN`, `OTA DATA`, `OTA END`,
`OTA APPLY`, `OTA ABORT`, and `OTA STATUS?`. OTA images are staged in W25Q16
before the bootloader installs them into the internal Flash app partition.
```
````

- [ ] **Step 2: Run all guard tests**

Run:

```powershell
Get-ChildItem tests\*.ps1 | ForEach-Object {
    powershell -ExecutionPolicy Bypass -File $_.FullName
}
```

Expected: every script prints its `... OK` line and exits with code 0.

- [ ] **Step 3: Build all required envs**

Run:

```bash
pio run -e genericSTM32F103RC
pio run -e app
pio run -e bootloader
```

Expected: all three builds end with `SUCCESS`. Record final RAM/Flash usage for `app` and `bootloader` in the implementation final response.

- [ ] **Step 4: Inspect generated artifacts**

Run:

```powershell
Get-ChildItem .pio\build\app, .pio\build\bootloader -Filter *.elf | Select-Object FullName, Length
Get-ChildItem .pio\build\app, .pio\build\bootloader -Filter *.bin | Select-Object FullName, Length
```

Expected: app and bootloader each produce `.elf` and `.bin` artifacts. Bootloader `.bin` must be less than 32768 bytes.

- [ ] **Step 5: Confirm only intended files are changed**

Run:

```bash
git status --short
```

Expected: only OTA implementation files and README changes are listed, plus pre-existing untracked `.tmp_schematic/` and modified `.codegraph/daemon.pid` if they are still present.

- [ ] **Step 6: Commit final docs and verification guards if not already committed**

```bash
git add README.md
git commit -m "docs: document OTA build flow"
```

If `README.md` is already committed as part of a preceding task, skip this commit and mention that in the final response.

---

## Manual Device Check

After all build checks pass, use hardware to verify the end-to-end path:

1. Flash `bootloader` at `0x08000000`.
2. Flash the current `app` at `0x08008000`.
3. Boot the device and confirm normal UI/storage behavior.
4. Send `OTA STATUS?` over USART1 and expect an idle response.
5. Send a small known app `.bin` using `OTA BEGIN`, repeated `OTA DATA` chunks of 16 bytes, `OTA END`, then `OTA APPLY`.
6. Confirm the device resets, bootloader installs the image, and the app starts.
7. Repeat with one corrupted `OTA DATA` block and confirm the app responds `ERR OTA CRC`.
8. Repeat with a corrupted whole-image CRC and confirm `OTA END` responds `ERR OTA IMAGECRC`.
9. Confirm EEPROM calibration values are still present.
10. Confirm LittleFS values `boot_count`, `battery_hist`, and `uptime_ckpt` survive the migration path on a device with old data.
