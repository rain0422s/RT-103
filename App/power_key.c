#include "power_key.h"
#include "led_control.h"
#include "key.h"
#include "lfs_port.h"
#include "storage.h"
#include "gpio.h"
#include "sensor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "timers.h"
#include <stdio.h>

#define POWER_KEY_EVENT      (0x01u << 6)
#define POWER_KEY_2S_ELAPSED (0x01u << 7)  /* 2s 定时到，由定时器回调置位 */
#define POWERKEY_GPIO_PORT   GPIOB
#define POWERKEY_GPIO_PIN    GPIO_PIN_11
/** 电源键已按下（低电平） */
#define power_key_is_pressed()   (HAL_GPIO_ReadPin(POWERKEY_GPIO_PORT, POWERKEY_GPIO_PIN) == GPIO_PIN_RESET)
/** 电源键已松开（高电平）则退出“等 2s” */
#define power_key_is_released()  (HAL_GPIO_ReadPin(POWERKEY_GPIO_PORT, POWERKEY_GPIO_PIN) == GPIO_PIN_SET)

static EventGroupHandle_t power_key_evgrp = NULL;
static TimerHandle_t xLedTimer = NULL;
/* Enter true immediately on power-key IRQ; cleared if shutdown is canceled. */
static volatile bool s_shutdown_active = false;

static void power_key_task(void *arg);
static void vTimerCallback(TimerHandle_t xTimer);

static void power_key_task(void *arg)
{
	ButtonState buttonState = IDLE_STATE;
	EventBits_t r_event;
	(void)arg;

	if (!power_key_evgrp) {
		DBG_PRINTF("[power] event group is NULL, task exit\n");
		vTaskDelete(NULL);
		return;
	}
	DBG_PRINTF("[power] task started, waiting for POWER_KEY_EVENT\n");
	for (;;) {
		r_event = xEventGroupWaitBits(power_key_evgrp, POWER_KEY_EVENT,
					     pdTRUE, pdFALSE, portMAX_DELAY);
		if ((r_event & POWER_KEY_EVENT) == 0)
			continue;

		xEventGroupClearBits(power_key_evgrp, POWER_KEY_2S_ELAPSED);
		if (xTimerReset(xLedTimer, 0) != pdPASS) {
			DBG_PRINTF("[power] Timer reset failed\n");
			s_shutdown_active = false;
			continue;
		}

		/* 等：按键松开 或 2s 到（由定时器回调置位） */
		for (;;) {
			r_event = xEventGroupWaitBits(power_key_evgrp,
				POWER_KEY_2S_ELAPSED, pdFALSE, pdFALSE, pdMS_TO_TICKS(10));
			if ((r_event & POWER_KEY_2S_ELAPSED) != 0)
				break;
			if (power_key_is_released())
				break;
		}

		(void)xTimerStop(xLedTimer, 0);

		if ((xEventGroupGetBits(power_key_evgrp) & POWER_KEY_2S_ELAPSED) != 0) {
			led_control_send(LED_CMD_ALL_OFF);
			buttonState = IDLE_STATE;
			if (button_scan(true, &buttonState) == SHORT_PRESS_STATE) {
				if (storage_flash_is_present())
					lfs_unmount_fs();
				led_control_send(LED_CMD_BLINK_4S);
				vTaskDelay(pdMS_TO_TICKS(4000));
				PowerDown;
			} else {
				led_control_send(LED_CMD_BREATH_ON);
				s_shutdown_active = false;
			}
		} else {
			/* Released before 2s, cancel shutdown flow and resume tasks. */
			s_shutdown_active = false;
		}
	}
}

static void vTimerCallback(TimerHandle_t xTimer)
{
	(void)xTimer;
	xEventGroupSetBits(power_key_evgrp, POWER_KEY_2S_ELAPSED);
}

int power_key_create(void)
{
	power_key_evgrp = xEventGroupCreate();
	if (!power_key_evgrp) {
		DBG_PRINTF("[Creator] xEventGroupCreate failed\n");
		return 0;
	}
	xLedTimer = xTimerCreate("PowerKey2s", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, vTimerCallback);
	if (!xLedTimer) {
		DBG_PRINTF("[Creator] xTimerCreate failed\n");
		return 0;
	}
	/* Keep highest priority below configMAX_PRIORITIES-1 after trimming. */
	if (xTaskCreate(power_key_task, "power_key", 128, NULL, 11, NULL) != pdPASS) {
		DBG_PRINTF("[Creator] power_key_task create failed\n");
		return 0;
	}
	return 1;
}

bool power_key_shutdown_active(void)
{
	return s_shutdown_active;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	BaseType_t woken = pdFALSE;

	if (GPIO_Pin == POWERKEY_GPIO_PIN && power_key_is_pressed()) {
		s_shutdown_active = true;
		xEventGroupSetBitsFromISR(power_key_evgrp, POWER_KEY_EVENT, &woken);
		portYIELD_FROM_ISR(woken);
		return;
	}

	if (GPIO_Pin == lis2dh12_INT2_Pin) {
		sensor_notify_motion_irq();
	}
}
