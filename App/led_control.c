#include "led_control.h"
#include "gpio.h"
#include "tim.h"
#include "utils.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#define BLUE_LED(x) do { (x) ? \
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET) : \
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET); \
} while (0)

#define LED_TICK_MS           2    /* 统一节拍 2ms，闪烁与呼吸都用此刻度 */
#define BLINK_TOGGLE_PERIOD   50   /* 50×2ms = 100ms 翻转一次 */
#define BLINK_CYCLES_4S       20
#define BREATH_PHASE_MAX      1000
#define BREATH_STEP           2    /* 每 2ms 步进，约 1s 半周期，更明显 */

QueueHandle_t led_ctl_queue = NULL;

static volatile bool breathing_led_enabled = false;

/* 统一 2ms 定时器：驱动闪烁 + 呼吸，由高优先级定时器任务执行，减少停顿 */
static TimerHandle_t led_tick_timer = NULL;
static volatile uint16_t blink_toggle_count = 0;
static uint8_t blink_subticks = 0;
static volatile uint8_t blink_led_state = 0;

static uint16_t breath_phase = 0;       /* 0..BREATH_PHASE_MAX */
static int16_t breath_direction = BREATH_STEP;

static void led_tick_cb(TimerHandle_t xTimer)
{
	(void)xTimer;

	/* 闪烁：每 100ms 翻转一次蓝灯 */
	if (blink_toggle_count > 0) {
		blink_subticks++;
		if (blink_subticks >= BLINK_TOGGLE_PERIOD) {
			blink_subticks = 0;
			blink_led_state = !blink_led_state;
			BLUE_LED(blink_led_state);
			blink_toggle_count--;
		}
	}

	/* 呼吸：2ms 步进 + 平方曲线，暗部变化更明显 */
	if (breathing_led_enabled) {
		breath_phase += breath_direction;
		if (breath_phase >= BREATH_PHASE_MAX) {
			breath_phase = BREATH_PHASE_MAX;
			breath_direction = -BREATH_STEP;
		} else if (breath_phase <= 0) {
			breath_phase = 0;
			breath_direction = BREATH_STEP;
		}
		/* 平方曲线：暗处变化更陡；TIM2 本板 0=亮 999=灭 */
		uint32_t v = (uint32_t)breath_phase * breath_phase / BREATH_PHASE_MAX;
		__HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, (uint32_t)v > 999 ? 999 : (uint16_t)v);
	}

	/* 无闪烁且无呼吸时停定时器 */
	if (blink_toggle_count == 0 && !breathing_led_enabled && led_tick_timer != NULL)
		xTimerStop(led_tick_timer, 0);
}

static void led_tick_ensure_running(void)
{
	if (led_tick_timer == NULL) {
		led_tick_timer = xTimerCreate("led_tick", pdMS_TO_TICKS(LED_TICK_MS),
					      pdTRUE, NULL, led_tick_cb);
	}
	if (led_tick_timer != NULL && xTimerIsTimerActive(led_tick_timer) == pdFALSE)
		xTimerStart(led_tick_timer, 0);
}

/** 启动 4s 闪烁（由 2ms 定时器驱动），仅内部/命令使用 */
static void led_blink_start_4s(void)
{
	blink_toggle_count = BLINK_CYCLES_4S * 2;
	blink_subticks = 0;
	blink_led_state = 0;
	BLUE_LED(0);
	led_tick_ensure_running();
}

/** 阻塞式闪烁（供 power_key 关机前用） */
void led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms)
{
	TickType_t on_ticks = pdMS_TO_TICKS(on_ms);
	TickType_t off_ticks = pdMS_TO_TICKS(off_ms);
	for (uint8_t i = 0; i < times; i++) {
		BLUE_LED(1);
		vTaskDelay(on_ticks);
		BLUE_LED(0);
		if (off_ms > 0 && i < times - 1)
			vTaskDelay(off_ticks);
	}
}

/** 呼吸灯改为定时器驱动，此处保留空实现供 sensor_task 等调用 */
void breathing_led_run_once(void)
{
	(void)0;
}

void breathing_led_set(bool on)
{
	breathing_led_enabled = on;
	if (!on) {
		__HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, 999);
		breath_phase = 0;
		breath_direction = BREATH_STEP;
	} else {
		led_tick_ensure_running();
	}
}

void led_control_send(uint8_t cmd)
{
	if (led_ctl_queue)
		xQueueSend(led_ctl_queue, &cmd, 0);
}

void led_control_task(void *arg)
{
	uint8_t cmd;
	(void)arg;

	for (;;) {
		if (xQueueReceive(led_ctl_queue, &cmd, portMAX_DELAY) != pdPASS)
			continue;

		blink_toggle_count = 0;

		switch (cmd) {
		case LED_CMD_BLINK_4S:
			led_blink_start_4s();
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
			breathing_led_set(false);
			/* POWER_LED is active-low on PA15/TIM2_CH1; keep it on for board debug. */
			__HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, 0);
			led_blink_start_4s();
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
