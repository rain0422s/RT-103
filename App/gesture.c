#include "gesture.h"
#include "power_key.h"
#include "storage.h"
#include "key.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>

static QueueHandle_t s_gesture_evt_queue = NULL;

bool gesture_key_event_get(gesture_key_event_t *evt)
{
	if (evt == NULL || s_gesture_evt_queue == NULL)
		return false;
	return xQueueReceive(s_gesture_evt_queue, evt, 0) == pdPASS;
}

void gesture_task(void *arg)
{
	ButtonState buttonState = IDLE_STATE;
	(void)arg;
	if (s_gesture_evt_queue == NULL) {
		s_gesture_evt_queue = xQueueCreate(8, sizeof(gesture_key_event_t));
	}

	while (!storage_system_ready()) {
		vTaskDelay(pdMS_TO_TICKS(50));
	}

	for (;;) {
		if (power_key_shutdown_active()) {
			vTaskDelay(pdMS_TO_TICKS(20));
			continue;
		}
		ButtonState evt = button_scan(false, &buttonState);

		switch (evt) {
		case SHORT_PRESS_STATE:
			DBG_PRINTF("[key] short press\n");
			if (s_gesture_evt_queue != NULL) {
				const gesture_key_event_t e = GESTURE_KEY_SINGLE_CLICK;
				(void)xQueueSend(s_gesture_evt_queue, &e, 0);
			}
			break;
		case LONG_PRESS_STATE:
			DBG_PRINTF("[key] long press\n");
			if (s_gesture_evt_queue != NULL) {
				const gesture_key_event_t e = GESTURE_KEY_LONG_PRESS;
				(void)xQueueSend(s_gesture_evt_queue, &e, 0);
			}
			break;
		case DOUBLE_PRESS_STATE:
			DBG_PRINTF("[key] double press\n");
			if (s_gesture_evt_queue != NULL) {
				const gesture_key_event_t e = GESTURE_KEY_DOUBLE_CLICK;
				(void)xQueueSend(s_gesture_evt_queue, &e, 0);
			}
			break;
		default:
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(5));   /* 5ms 轮询，配合 key 10ms 扫描更灵敏 */
	}
}
