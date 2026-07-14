#include "boot_jump.h"
#include "boot_ota.h"
#include "ota_boot_state.h"
#include "spi.h"
#include "stm32f1xx_hal.h"
#include "w25qxx.h"
#include <stdbool.h>
#include <string.h>

#define BOOT_POWER_HOLD_GPIO_PORT GPIOB
#define BOOT_POWER_HOLD_GPIO_PIN  GPIO_PIN_10
#define BOOT_POWER_KEY_GPIO_PORT  GPIOB
#define BOOT_POWER_KEY_GPIO_PIN   GPIO_PIN_11
#define BOOT_POWER_KEY_LATCH_MS   20U

#ifndef BOOT_DIAG_UART
#define BOOT_DIAG_UART 0
#endif

#if BOOT_DIAG_UART
#define BOOT_DIAG_START_DELAY_MS 1000U
#endif

static bool s_boot_power_key_latched;

#if BOOT_DIAG_UART
static UART_HandleTypeDef s_boot_uart;

static void boot_diag_early_putc(char ch)
{
	while ((USART1->SR & USART_SR_TXE) == 0U) {
	}
	USART1->DR = (uint16_t)ch;
}

static void boot_diag_early_puts(const char *text)
{
	while (*text != '\0') {
		boot_diag_early_putc(*text++);
	}
	while ((USART1->SR & USART_SR_TC) == 0U) {
	}
}

static void boot_diag_early_probe(void)
{
	volatile uint32_t settle;

	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN |
			RCC_APB2ENR_IOPBEN | RCC_APB2ENR_USART1EN;
	settle = RCC->APB2ENR;
	(void)settle;

	GPIOB->BSRR = GPIO_PIN_10;
	GPIOB->CRH = (GPIOB->CRH & ~(0xFUL << 8U)) | (0x2UL << 8U);

	GPIOA->CRH = (GPIOA->CRH & ~((0xFUL << 4U) | (0xFUL << 8U))) |
		     (0xBUL << 4U) | (0x4UL << 8U);
	USART1->BRR = 8000000UL / 115200UL;
	USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
	boot_diag_early_puts("Z\r\n");
}
#endif

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

#if BOOT_DIAG_UART
static void boot_diag_uart_init(void)
{
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_USART1_CLK_ENABLE();

	gpio.Pin = GPIO_PIN_9;
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOA, &gpio);

	gpio.Pin = GPIO_PIN_10;
	gpio.Mode = GPIO_MODE_INPUT;
	gpio.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &gpio);

	s_boot_uart.Instance = USART1;
	s_boot_uart.Init.BaudRate = 115200;
	s_boot_uart.Init.WordLength = UART_WORDLENGTH_8B;
	s_boot_uart.Init.StopBits = UART_STOPBITS_1;
	s_boot_uart.Init.Parity = UART_PARITY_NONE;
	s_boot_uart.Init.Mode = UART_MODE_TX_RX;
	s_boot_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	s_boot_uart.Init.OverSampling = UART_OVERSAMPLING_16;
	(void)HAL_UART_Init(&s_boot_uart);
}

void boot_diag_puts(const char *text)
{
	(void)HAL_UART_Transmit(&s_boot_uart, (uint8_t *)text,
				(uint16_t)strlen(text), 200);
}
#else
static void boot_diag_uart_init(void) {}
void boot_diag_puts(const char *text) { (void)text; }
#endif

static bool boot_power_key_pressed(void)
{
	return HAL_GPIO_ReadPin(BOOT_POWER_KEY_GPIO_PORT,
				BOOT_POWER_KEY_GPIO_PIN) == GPIO_PIN_RESET;
}

static void boot_power_key_latch_sample(void)
{
	s_boot_power_key_latched = boot_power_key_pressed();
	for (uint32_t i = 0; !s_boot_power_key_latched &&
	     i < BOOT_POWER_KEY_LATCH_MS; i++) {
		HAL_Delay(1);
		s_boot_power_key_latched = boot_power_key_pressed();
	}
}

static void boot_gpio_init(void)
{
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	HAL_GPIO_WritePin(BOOT_POWER_HOLD_GPIO_PORT,
			  BOOT_POWER_HOLD_GPIO_PIN,
			  GPIO_PIN_SET);
	gpio.Pin = BOOT_POWER_HOLD_GPIO_PIN;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(BOOT_POWER_HOLD_GPIO_PORT, &gpio);

	gpio.Pin = BOOT_POWER_KEY_GPIO_PIN;
	gpio.Mode = GPIO_MODE_INPUT;
	gpio.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(BOOT_POWER_KEY_GPIO_PORT, &gpio);
	boot_power_key_latch_sample();

	HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
	gpio.Pin = CS_Pin;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(CS_GPIO_Port, &gpio);
}

static bool boot_spi1_init(void)
{
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_SPI1_CLK_ENABLE();

	gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOA, &gpio);

	gpio.Pin = GPIO_PIN_6;
	gpio.Mode = GPIO_MODE_INPUT;
	gpio.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &gpio);

	hspi1.Instance = SPI1;
	hspi1.Init.Mode = SPI_MODE_MASTER;
	hspi1.Init.Direction = SPI_DIRECTION_2LINES;
	hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi1.Init.NSS = SPI_NSS_SOFT;
	hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
	hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
	hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	hspi1.Init.CRCPolynomial = 10;
	return HAL_SPI_Init(&hspi1) == HAL_OK;
}

int main(void)
{
	uint32_t boot_slot = OTA_SLOT_A;
	uint32_t boot_base;
	ota_boot_state_t boot_state;
	bool rolled_back = false;

#if BOOT_DIAG_UART
	boot_diag_early_probe();
#endif
	HAL_Init();
#if BOOT_DIAG_UART
	boot_diag_early_puts("H\r\n");
#endif
	boot_gpio_init();
#if BOOT_DIAG_UART
	boot_diag_early_puts("G\r\n");
#endif
	BootClock_Config();
#if BOOT_DIAG_UART
	boot_diag_early_puts("C0\r\n");
#endif
	boot_diag_uart_init();
#if BOOT_DIAG_UART
	boot_diag_early_puts("U\r\n");
#endif
#if BOOT_DIAG_UART
	HAL_Delay(BOOT_DIAG_START_DELAY_MS);
	boot_diag_early_puts("T\r\n");
#endif
	boot_diag_puts("A\r\n");
	boot_diag_puts(s_boot_power_key_latched ? "P1\r\n" : "P0\r\n");
	if (boot_spi1_init()) {
		boot_diag_puts("B\r\n");
		w25qxx_reset();
		boot_diag_puts("C\r\n");
		if (boot_power_key_pressed())
			boot_diag_puts("K\r\n");
		(void)boot_ota_apply_if_pending();
		boot_diag_puts("D\r\n");
	} else {
		boot_diag_puts("S\r\n");
	}

	if (!ota_boot_state_select_boot_slot(&boot_state, &boot_slot,
					     &rolled_back)) {
		ota_boot_state_defaults(&boot_state);
		boot_slot = boot_state.active_slot;
	}
	if (rolled_back)
		boot_ota_mark_confirm_timeout(boot_state.pending_version);
	boot_base = ota_boot_state_slot_base(boot_slot);

	if (boot_app_is_valid_at(boot_base)) {
		boot_diag_puts("J\r\n");
		boot_jump_to_slot(boot_base);
	}

	boot_diag_puts("N\r\n");
	while (1) {
		HAL_Delay(250);
	}
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}

void Error_Handler(void)
{
	while (1) {
	}
}
