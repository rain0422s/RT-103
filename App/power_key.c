#include "power_key.h"
#include "led_control.h"
#include "key.h"
#include "lfs_port.h"
#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "timers.h"
#include <stdio.h>

#define POWER_KEY_EVENT  (0x01 << 6)
#define POWERKEY_GPIO_PORT  GPIOB
#define POWERKEY_GPIO_PIN   GPIO_PIN_11

static EventGroupHandle_t myxEventGroupHandle_t = NULL;
static TimerHandle_t xLedTimer = NULL;
static volatile bool time_flag = false;

static void power_key_task(void *arg);
static void vTimerCallback(TimerHandle_t xTimer);

static void power_key_task(void *arg)
{
	ButtonState buttonState = IDLE_STATE;
	EventBits_t r_event;
	(void)arg;

	if (!myxEventGroupHandle_t) {
		printf("[power] event group is NULL, task exit\n");
		vTaskDelete(NULL);
		return;
	}
	printf("[power] task started, waiting for POWER_KEY_EVENT\n");
	for (;;) {
		r_event = xEventGroupWaitBits(myxEventGroupHandle_t, POWER_KEY_EVENT,
					     pdTRUE, pdFALSE, portMAX_DELAY);
		if ((r_event & POWER_KEY_EVENT) == 0)
			continue;

		time_flag = false;
		if (xTimerReset(xLedTimer, 0) != pdPASS) {
			printf("[power] Timer reset failed\n");
			continue;
		}

		while (!HAL_GPIO_ReadPin(POWERKEY_GPIO_PORT, POWERKEY_GPIO_PIN) && !time_flag)
			vTaskDelay(pdMS_TO_TICKS(10));

		if (xTimerStop(xLedTimer, 0) != pdPASS) {
			printf("[power] Timer stop failed\n");
			continue;
		}

		if (time_flag) {
			buttonState = IDLE_STATE;
			if (button_scan(true, &buttonState) == LONG_PRESS_STATE) {
				led_control_send(LED_CMD_ALL_OFF);
				lfs_unmount_fs();
				led_blink(20, 100, 100);
				PowerDown;
			}
		}
	}
}

static void vTimerCallback(TimerHandle_t xTimer)
{
	(void)xTimer;
	time_flag = true;
}

int power_key_create(void)
{
	myxEventGroupHandle_t = xEventGroupCreate();
	if (!myxEventGroupHandle_t) {
		printf("[Creator] xEventGroupCreate failed\n");
		return 0;
	}
	xLedTimer = xTimerCreate("PowerKey2s", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, vTimerCallback);
	if (!xLedTimer) {
		printf("[Creator] xTimerCreate failed\n");
		return 0;
	}
	if (xTaskCreate(power_key_task, "power_key", 128, NULL, 2, NULL) != pdPASS) {
		printf("[Creator] power_key_task create failed\n");
		return 0;
	}
	return 1;
}


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
	uint32_t ulReturn;
	uint16_t event = 0;

	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	if (GPIO_Pin == POWERKEY_GPIO_PIN &&
	    HAL_GPIO_ReadPin(POWERKEY_GPIO_PORT, GPIO_Pin) == GPIO_PIN_RESET) {
		event = POWER_KEY_EVENT;
		xEventGroupSetBitsFromISR(myxEventGroupHandle_t, event, &pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
	}
	taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}
