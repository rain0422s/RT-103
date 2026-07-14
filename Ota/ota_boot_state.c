#include "ota_boot_state.h"

#include "ota_crc32.h"
#include "ota_layout.h"
#include "stm32f1xx_hal.h"
#include <string.h>

static bool ota_boot_state_pending_slot_is_valid(uint32_t slot)
{
        return slot == OTA_BOOT_STATE_INVALID_SLOT ||
               ota_boot_state_slot_is_valid(slot);
}

static uint32_t ota_boot_state_crc(const ota_boot_state_t *state)
{
        ota_boot_state_t copy;

        if (state == 0)
                return 0UL;
        copy = *state;
        copy.state_crc32 = 0UL;
        return ota_crc32_compute(&copy, (uint32_t)sizeof(copy));
}

static void ota_boot_state_finalize(ota_boot_state_t *state)
{
        if (state == 0)
                return;
        state->state_crc32 = 0UL;
        state->state_crc32 = ota_boot_state_crc(state);
}

static bool ota_boot_state_is_valid(const ota_boot_state_t *state)
{
        if (state == 0)
                return false;
        if (state->magic != OTA_BOOT_STATE_MAGIC)
                return false;
        if (state->version != OTA_BOOT_STATE_VERSION)
                return false;
        if (state->header_size != (uint16_t)sizeof(*state))
                return false;
        if (!ota_boot_state_slot_is_valid(state->active_slot))
                return false;
        if (!ota_boot_state_slot_is_valid(state->confirmed_slot))
                return false;
        if (!ota_boot_state_pending_slot_is_valid(state->pending_slot))
                return false;
        if (state->boot_attempts > OTA_BOOT_CONFIRM_MAX_ATTEMPTS)
                return false;
        if (state->state_crc32 != ota_boot_state_crc(state))
                return false;
        return true;
}

static bool ota_boot_state_read_page(uint32_t address, ota_boot_state_t *state)
{
        const ota_boot_state_t *stored = (const ota_boot_state_t *)address;

        if (state == 0)
                return false;
        memcpy(state, stored, sizeof(*state));
        return ota_boot_state_is_valid(state);
}

static bool ota_boot_state_program_page(uint32_t address,
                                        const ota_boot_state_t *state)
{
        FLASH_EraseInitTypeDef erase = {0};
        HAL_StatusTypeDef status;
        uint32_t page_error = 0;
        const uint8_t *data = (const uint8_t *)state;

        if (state == 0)
                return false;
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = address;
        erase.NbPages = 1;

        if (HAL_FLASH_Unlock() != HAL_OK)
                return false;
        status = HAL_FLASHEx_Erase(&erase, &page_error);
        if (status == HAL_OK && page_error == 0xFFFFFFFFUL) {
                for (uint32_t i = 0; i < sizeof(*state); i += 2UL) {
                        uint16_t halfword = data[i];
                        if (i + 1UL < sizeof(*state))
                                halfword |= ((uint16_t)data[i + 1UL] << 8U);
                        else
                                halfword |= 0xFF00U;
                        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                                   address + i, halfword);
                        if (status != HAL_OK)
                                break;
                }
        }
        (void)HAL_FLASH_Lock();
        return status == HAL_OK && page_error == 0xFFFFFFFFUL;
}

void ota_boot_state_defaults(ota_boot_state_t *state)
{
        if (state == 0)
                return;
        memset(state, 0, sizeof(*state));
        state->magic = OTA_BOOT_STATE_MAGIC;
        state->version = OTA_BOOT_STATE_VERSION;
        state->header_size = (uint16_t)sizeof(*state);
        state->active_slot = OTA_SLOT_A;
        state->confirmed_slot = OTA_SLOT_A;
        state->pending_slot = OTA_BOOT_STATE_INVALID_SLOT;
        state->accepted_version = 0UL;
        state->pending_version = 0UL;
        state->boot_attempts = 0UL;
        state->sequence = 0UL;
        state->last_failure_reason = 0UL;
        ota_boot_state_finalize(state);
}

bool ota_boot_state_read(ota_boot_state_t *state)
{
        ota_boot_state_t primary;
        ota_boot_state_t backup;
        bool primary_ok;
        bool backup_ok;

        if (state == 0)
                return false;

        primary_ok = ota_boot_state_read_page(OTA_BOOT_STATE_PRIMARY_ADDR,
                                              &primary);
        backup_ok = ota_boot_state_read_page(OTA_BOOT_STATE_BACKUP_ADDR,
                                             &backup);
        if (primary_ok && (!backup_ok || primary.sequence >= backup.sequence)) {
                *state = primary;
                return true;
        }
        if (backup_ok) {
                *state = backup;
                return true;
        }

        ota_boot_state_defaults(state);
        return false;
}

bool ota_boot_state_write(ota_boot_state_t *state)
{
        ota_boot_state_t copy;
        bool primary_ok;
        bool backup_ok;

        if (state == 0)
                return false;
        copy = *state;
        copy.sequence++;
        ota_boot_state_finalize(&copy);
        primary_ok = ota_boot_state_program_page(OTA_BOOT_STATE_PRIMARY_ADDR,
                                                 &copy);
        backup_ok = ota_boot_state_program_page(OTA_BOOT_STATE_BACKUP_ADDR,
                                                &copy);
        if (primary_ok || backup_ok) {
                *state = copy;
                return true;
        }
        return false;
}

bool ota_boot_state_slot_is_valid(uint32_t slot)
{
        return slot == OTA_SLOT_A || slot == OTA_SLOT_B;
}

uint32_t ota_boot_state_current_slot(void)
{
#if defined(OTA_APP_SLOT_B)
        return OTA_SLOT_B;
#else
        return OTA_SLOT_A;
#endif
}

uint32_t ota_boot_state_other_slot(uint32_t slot)
{
        if (slot == OTA_SLOT_A)
                return OTA_SLOT_B;
        if (slot == OTA_SLOT_B)
                return OTA_SLOT_A;
        return OTA_BOOT_STATE_INVALID_SLOT;
}

uint32_t ota_boot_state_slot_base(uint32_t slot)
{
        if (slot == OTA_SLOT_A)
                return OTA_APP_SLOT_A_BASE;
        if (slot == OTA_SLOT_B)
                return OTA_APP_SLOT_B_BASE;
        return 0UL;
}

bool ota_boot_state_select_boot_slot(ota_boot_state_t *state,
                                     uint32_t *slot,
                                     bool *rolled_back)
{
        ota_boot_state_t next;
        bool changed = false;

        if (slot == 0)
                return false;
        (void)ota_boot_state_read(&next);
        if (rolled_back != 0)
                *rolled_back = false;

        if (ota_boot_state_slot_is_valid(next.pending_slot)) {
                if (next.boot_attempts >= OTA_BOOT_CONFIRM_MAX_ATTEMPTS) {
                        next.active_slot = next.confirmed_slot;
                        next.pending_slot = OTA_BOOT_STATE_INVALID_SLOT;
                        next.pending_version = 0UL;
                        next.boot_attempts = 0UL;
                        next.last_failure_reason = 1UL;
                        changed = true;
                        if (rolled_back != 0)
                                *rolled_back = true;
                } else {
                        next.active_slot = next.pending_slot;
                        next.boot_attempts++;
                        changed = true;
                }
        } else if (!ota_boot_state_slot_is_valid(next.active_slot)) {
                next.active_slot = next.confirmed_slot;
                changed = true;
        }

        if (changed && !ota_boot_state_write(&next))
                return false;
        if (state != 0)
                *state = next;
        *slot = next.active_slot;
        return true;
}

bool ota_boot_state_set_pending(uint32_t slot, uint32_t firmware_version)
{
        ota_boot_state_t state;

        if (!ota_boot_state_slot_is_valid(slot))
                return false;
        (void)ota_boot_state_read(&state);
        state.active_slot = slot;
        state.pending_slot = slot;
        state.pending_version = firmware_version;
        state.boot_attempts = 0UL;
        state.last_failure_reason = 0UL;
        return ota_boot_state_write(&state);
}

bool ota_boot_state_confirm_current(uint32_t firmware_version)
{
        ota_boot_state_t state;
        uint32_t current = ota_boot_state_current_slot();

        (void)ota_boot_state_read(&state);
        if (!ota_boot_state_slot_is_valid(current))
                return false;
        if (state.confirmed_slot == current &&
            state.pending_slot == OTA_BOOT_STATE_INVALID_SLOT)
                return true;

        state.active_slot = current;
        state.confirmed_slot = current;
        state.pending_slot = OTA_BOOT_STATE_INVALID_SLOT;
        if (state.pending_version > firmware_version)
                firmware_version = state.pending_version;
        if (firmware_version > state.accepted_version)
                state.accepted_version = firmware_version;
        state.pending_version = 0UL;
        state.boot_attempts = 0UL;
        state.last_failure_reason = 0UL;
        return ota_boot_state_write(&state);
}
