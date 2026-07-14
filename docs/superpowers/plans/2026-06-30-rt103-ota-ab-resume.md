# RT-103 OTA A/B Resume Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend RT-103 OTA with manifest v2, firmware version anti-rollback, resumable transfer, direct binary transfer, and internal Flash A/B app slots.

**Architecture:** Keep the current USART1 text OTA commands as a fallback. Add bootloader-owned boot state in reserved internal Flash pages, use W25Q16 only as the staging area, and let the bootloader install a staged image into the inactive app slot before switching active slot and waiting for app confirmation.

**Tech Stack:** STM32F103RCT6, PlatformIO, ARM GCC, PowerShell guards, W25Q16 staging flash, internal Flash boot state pages.

---

### Task 1: RED Guards

**Files:**
- Create: `tests/ota_manifest_v2_guard_check.ps1`
- Create: `tests/ota_ab_slot_guard_check.ps1`
- Create: `tests/ota_resume_binary_guard_check.ps1`
- Modify: `tests/ota_manifest_guard_check.ps1`
- Modify: `tests/ota_layout_crc_guard_check.ps1`
- Modify: `tests/ota_partition_guard_check.ps1`

- [ ] Add guard checks for manifest v2, failure reasons, version fields, A/B slot constants, app A/B PlatformIO environments, resume commands, and binary protocol symbols.
- [ ] Run the new guards and confirm they fail because the implementation is missing.

### Task 2: Manifest v2 And Failure Reasons

**Files:**
- Modify: `Ota/ota_manifest.h`
- Modify: `Ota/ota_manifest.c`
- Modify: `Bootloader/boot_ota.c`
- Modify: `App/ota_update.c`

- [ ] Add `ota_failure_reason_t`, `OTA_MANIFEST_VERSION 2U`, and v2 manifest fields.
- [ ] Initialize, validate, CRC, and persist failure reasons.
- [ ] Mark each bootloader failure path with a specific reason.
- [ ] Expose failure reason through `OTA STATUS?`.

### Task 3: Boot State And Anti-Rollback

**Files:**
- Create: `Ota/ota_boot_state.h`
- Create: `Ota/ota_boot_state.c`
- Modify: `Ota/ota_layout.h`
- Modify: `Bootloader/boot_ota.c`
- Modify: `Bootloader/main.c`

- [ ] Add boot state metadata in internal Flash page `0x08007000` plus redundant page `0x08007800`.
- [ ] Store active slot, confirmed slot, pending slot, accepted firmware version, pending firmware version, boot attempt counter, sequence, and CRC.
- [ ] Reject pending OTA when `firmware_version < accepted_version`.

### Task 4: Internal Flash A/B Slots

**Files:**
- Create: `STM32F103XX_APP_A.ld`
- Create: `STM32F103XX_APP_B.ld`
- Modify: `STM32F103XX_BOOTLOADER.ld`
- Modify: `platformio.ini`
- Modify: `Bootloader/boot_flash.c`
- Modify: `Bootloader/boot_flash.h`
- Modify: `Bootloader/boot_jump.c`
- Modify: `Bootloader/boot_ota.c`
- Modify: `Ota/ota_layout.h`

- [ ] Change bootloader code size to 28KB and reserve two 2KB boot-state pages.
- [ ] Define slot A at `0x08008000`, slot B at `0x08024000`, each 112KB.
- [ ] Add `app_a` and `app_b` PlatformIO environments with matching VTOR offsets.
- [ ] Program the pending image to the inactive target slot and boot that slot.

### Task 5: Confirm/Rollback

**Files:**
- Modify: `App/ota_update.c`
- Modify: `App/ota_update.h`
- Modify: `Core/Src/main.c`
- Modify: `Bootloader/main.c`

- [ ] Add an app-side confirmation API that marks the current pending slot as confirmed.
- [ ] Call confirmation after app initialization reaches a safe point.
- [ ] Roll back in bootloader if a pending slot does not confirm within the configured attempt count.

### Task 6: Resume And Binary Transfer

**Files:**
- Modify: `App/ota_update.c`
- Modify: `App/ota_update.h`
- Modify: `Ota/ota_layout.h`
- Modify: `App/uart_forward.c`

- [ ] Add `OTA RESUME?` returning the persisted receive offset and state.
- [ ] Persist receive checkpoints on 4KB boundaries.
- [ ] Add a direct binary packet parser for `OTAB` packets with sequence, offset, length, and CRC.
- [ ] Keep existing ASCII `OTA DATA` path working.

### Task 7: Verification And Documentation

**Files:**
- Modify: `docs/rt103_ota_implementation.md`
- Modify: `docs/rt103_ota_implementation.youdao.json`

- [ ] Run all OTA guards.
- [ ] Run `pio run -e bootloader -e app_a -e app_b`.
- [ ] Update the local OTA implementation article.
- [ ] Update Youdao note `27175D5BA09E4A2F8D79C0662CC591D3` and read it back.
