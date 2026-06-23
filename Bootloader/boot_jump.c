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
