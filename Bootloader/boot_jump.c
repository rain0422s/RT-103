#include "boot_jump.h"

#include "ota_layout.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#define BOOT_NVIC_IRQ_WORDS 8U

typedef void (*boot_app_entry_t)(void);

static void boot_flush_flash_prefetch(void)
{
#if defined(FLASH_ACR_PRFTBE)
	__HAL_FLASH_PREFETCH_BUFFER_DISABLE();
	__DSB();
	__ISB();
	__HAL_FLASH_PREFETCH_BUFFER_ENABLE();
	__DSB();
	__ISB();
#endif
}

static void boot_reset_dma_controller(void)
{
	const uint32_t ahbenr = RCC->AHBENR;

	__HAL_RCC_DMA1_CLK_ENABLE();
	DMA1_Channel1->CCR = 0U;
	DMA1_Channel2->CCR = 0U;
	DMA1_Channel3->CCR = 0U;
	DMA1_Channel4->CCR = 0U;
	DMA1_Channel5->CCR = 0U;
	DMA1_Channel6->CCR = 0U;
	DMA1_Channel7->CCR = 0U;
	DMA1->IFCR = 0x0FFFFFFFUL;

#if defined(DMA2)
	RCC->AHBENR |= RCC_AHBENR_DMA2EN;
	(void)RCC->AHBENR;
	DMA2_Channel1->CCR = 0U;
	DMA2_Channel2->CCR = 0U;
	DMA2_Channel3->CCR = 0U;
	DMA2_Channel4->CCR = 0U;
	DMA2_Channel5->CCR = 0U;
	DMA2->IFCR = 0x0FFFFFFFUL;
#endif

	RCC->AHBENR = ahbenr;
}

static void boot_reset_app_peripherals(void)
{
	const uint32_t apb1_reset =
		RCC_APB1RSTR_TIM2RST | RCC_APB1RSTR_TIM3RST |
#if defined(RCC_APB1RSTR_TIM4RST)
		RCC_APB1RSTR_TIM4RST |
#endif
		RCC_APB1RSTR_USART2RST | RCC_APB1RSTR_I2C1RST |
#if defined(RCC_APB1RSTR_I2C2RST)
		RCC_APB1RSTR_I2C2RST |
#endif
#if defined(RCC_APB1RSTR_SPI2RST)
		RCC_APB1RSTR_SPI2RST |
#endif
#if defined(RCC_APB1RSTR_SPI3RST)
		RCC_APB1RSTR_SPI3RST |
#endif
		0U;
	const uint32_t apb2_reset =
		RCC_APB2RSTR_ADC1RST |
#if defined(RCC_APB2RSTR_ADC2RST)
		RCC_APB2RSTR_ADC2RST |
#endif
		RCC_APB2RSTR_TIM1RST | RCC_APB2RSTR_SPI1RST |
		RCC_APB2RSTR_USART1RST;

	boot_reset_dma_controller();
	RCC->APB1RSTR |= apb1_reset;
	RCC->APB2RSTR |= apb2_reset;
	__DSB();
	RCC->APB1RSTR &= ~apb1_reset;
	RCC->APB2RSTR &= ~apb2_reset;
	__DSB();
	__ISB();
}

bool boot_app_is_valid_at(uint32_t app_base)
{
	const uint32_t msp = *(volatile uint32_t *)app_base;
	const uint32_t reset = *(volatile uint32_t *)(app_base + 4UL);

	if (app_base != OTA_APP_SLOT_A_BASE && app_base != OTA_APP_SLOT_B_BASE)
		return false;
	if (msp < OTA_SRAM_BASE || msp > OTA_SRAM_END)
		return false;
	if (reset < app_base || reset >= app_base + OTA_APP_SLOT_SIZE)
		return false;
	if ((reset & 1UL) == 0UL)
		return false;
	return true;
}

bool boot_app_is_valid(void)
{
	return boot_app_is_valid_at(OTA_APP_BASE);
}

void boot_jump_to_slot(uint32_t app_base)
{
	const uint32_t app_msp = *(volatile uint32_t *)app_base;
	const uint32_t app_reset = *(volatile uint32_t *)(app_base + 4UL);
	const boot_app_entry_t app_entry = (boot_app_entry_t)app_reset;

	__disable_irq();
	HAL_RCC_DeInit();
	boot_reset_app_peripherals();
	boot_flush_flash_prefetch();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
	for (uint32_t i = 0; i < BOOT_NVIC_IRQ_WORDS; i++) {
		NVIC->ICER[i] = 0xFFFFFFFFUL;
		NVIC->ICPR[i] = 0xFFFFFFFFUL;
	}
	SCB->VTOR = app_base;
	__set_BASEPRI(0U);
	__set_FAULTMASK(0U);
	__set_PSP(0U);
	__set_CONTROL(0U);
	__set_MSP(app_msp);
	__DSB();
	__ISB();
	__enable_irq();
	app_entry();
}

void boot_jump_to_app(void)
{
	boot_jump_to_slot(OTA_APP_BASE);
}
