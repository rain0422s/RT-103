#include "led_control.h"
#include "gpio.h"
#include "tim.h"
#include "utils.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define BLUE_LED(x) do { (x) ? \
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET) : \
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET); \
} while (0)

QueueHandle_t led_ctl_queue = NULL;

static uint16_t pwmVal = 0;
static volatile bool breathing_led_enabled = false;

/** Blink BLUE_LED: times × (on_ms on, off_ms off). Uses delay_ms (blocking). */
void led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms)
{
	for (uint8_t i = 0; i < times; i++) {
		BLUE_LED(1);
		delay_ms(on_ms);
		BLUE_LED(0);
		if (off_ms > 0 && i < times - 1)
			delay_ms(off_ms);
	}
}

/** 呼吸灯：一次从暗到亮再到暗（循环内检查使能，关闭时可立即退出） */
static void breathing_led_once(void)
{
	while (pwmVal < 1000 && breathing_led_enabled) {
		pwmVal++;
		__HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, pwmVal);
		delay_ms(1);
	}
	if (!breathing_led_enabled)
		return;
	while (pwmVal && breathing_led_enabled) {
		pwmVal--;
		__HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, pwmVal);
		delay_ms(1);
	}
}

void breathing_led_run_once(void)
{
	if (breathing_led_enabled)
		breathing_led_once();
}

void breathing_led_set(bool on)
{
	breathing_led_enabled = on;
	if (!on) {
		pwmVal = 0;
		__HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, 999);
	}
}

void led_control_send(uint8_t cmd)
{
	if (led_ctl_queue)
		xQueueSend(led_ctl_queue, &cmd, 0);
}

/** 灯光控制任务：栈 96 字，接收 1/2/3/4/5 执行对应灯控 */
void led_control_task(void *arg)
{
	uint8_t cmd;
	(void)arg;

	for (;;) {
		if (xQueueReceive(led_ctl_queue, &cmd, portMAX_DELAY) != pdPASS)
			continue;

		switch (cmd) {
		case LED_CMD_BLINK_4S:
			led_blink(20, 100, 100);  /* 4s ≈ 20×(100+100)ms */
			break;
		case LED_CMD_OFF:
			BLUE_LED(0);
			break;
		case LED_CMD_ON:
			BLUE_LED(1);
			break;
		case LED_CMD_BREATH_ON:
			breathing_led_set(true);
			break;
		case LED_CMD_BREATH_OFF:
			breathing_led_set(false);
			break;
		case LED_CMD_READY:
			breathing_led_set(true);
			led_blink(20, 100, 100);
			break;
		case LED_CMD_ALL_OFF:
			breathing_led_set(false);
			BLUE_LED(0);
			break;
		default:
			break;
		}
	}
}
