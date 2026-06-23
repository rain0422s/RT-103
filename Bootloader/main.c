#include "boot_jump.h"
#include "boot_ota.h"
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
	if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
		while (1) {
		}
	}

	clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
			RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
	clk.APB1CLKDivider = RCC_HCLK_DIV1;
	clk.APB2CLKDivider = RCC_HCLK_DIV1;
	if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) {
		while (1) {
		}
	}
}

int main(void)
{
	HAL_Init();
	BootClock_Config();
	MX_GPIO_Init();
	MX_SPI1_Init();
	w25qxx_reset();
	(void)boot_ota_apply_if_pending();

	if (boot_app_is_valid())
		boot_jump_to_app();

	while (1) {
		HAL_Delay(250);
	}
}

void Error_Handler(void)
{
	while (1) {
	}
}
