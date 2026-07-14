#include "boot_flash.h"

#include "ota_layout.h"
#include "stm32f1xx_hal.h"

static bool boot_flash_slot_base_is_valid(uint32_t app_base)
{
	return app_base == OTA_APP_SLOT_A_BASE ||
	       app_base == OTA_APP_SLOT_B_BASE;
}

static bool boot_flash_range_is_valid(uint32_t address, uint32_t len)
{
	if (len == 0UL)
		return true;
	if (address >= OTA_APP_SLOT_A_BASE &&
	    address < OTA_APP_SLOT_A_BASE + OTA_APP_SLOT_SIZE)
		return len <= OTA_APP_SLOT_A_BASE + OTA_APP_SLOT_SIZE - address;
	if (address >= OTA_APP_SLOT_B_BASE &&
	    address < OTA_APP_SLOT_B_BASE + OTA_APP_SLOT_SIZE)
		return len <= OTA_APP_SLOT_B_BASE + OTA_APP_SLOT_SIZE - address;
	return false;
}

bool boot_flash_erase_slot(uint32_t app_base, uint32_t image_size)
{
	FLASH_EraseInitTypeDef erase = {0};
	uint32_t page_error = 0;
	uint32_t pages;
	HAL_StatusTypeDef status;

	if (!boot_flash_slot_base_is_valid(app_base) ||
	    image_size == 0UL || image_size > OTA_APP_SLOT_SIZE)
		return false;

	pages = (image_size + FLASH_PAGE_SIZE - 1UL) / FLASH_PAGE_SIZE;
	erase.TypeErase = FLASH_TYPEERASE_PAGES;
	erase.PageAddress = app_base;
	erase.NbPages = pages;

	if (HAL_FLASH_Unlock() != HAL_OK)
		return false;
	status = HAL_FLASHEx_Erase(&erase, &page_error);
	(void)HAL_FLASH_Lock();
	return status == HAL_OK && page_error == 0xFFFFFFFFUL;
}

bool boot_flash_erase_app(uint32_t image_size)
{
	return boot_flash_erase_slot(OTA_APP_BASE, image_size);
}

bool boot_flash_program(uint32_t address, const uint8_t *data, uint32_t len)
{
	HAL_StatusTypeDef status = HAL_OK;

	if (len == 0UL)
		return true;
	if (data == 0 || !boot_flash_range_is_valid(address, len))
		return false;
	if ((address & 1UL) != 0UL)
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
