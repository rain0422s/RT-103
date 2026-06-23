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

	if (len == 0UL)
		return true;
	if (data == 0 || address < OTA_APP_BASE || address >= OTA_FLASH_END)
		return false;
	if ((address & 1UL) != 0UL || len > OTA_FLASH_END - address)
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
