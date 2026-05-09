#include "gesture.h"
#include "storage.h"
#include "key.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>

static QueueHandle_t s_gesture_evt_queue = NULL;

static void gesture_post_event(gesture_key_event_t e)
{
	if (s_gesture_evt_queue != NULL)
		(void)xQueueSend(s_gesture_evt_queue, &e, 0);
}

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
		ButtonState evt = button_scan(false, &buttonState);

		switch (evt) {
		case SHORT_PRESS_STATE:
			DBG_PRINTF("[key] short press\n");
			gesture_post_event(GESTURE_KEY_SINGLE_CLICK);
			break;
		case LONG_PRESS_STATE:
			DBG_PRINTF("[key] long press\n");
			gesture_post_event(GESTURE_KEY_LONG_PRESS);
			break;
		case DOUBLE_PRESS_STATE:
			DBG_PRINTF("[key] double press\n");
			gesture_post_event(GESTURE_KEY_DOUBLE_CLICK);
			break;
		default:
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(5));   /* 5ms 轮询，配合 key 10ms 扫描更灵敏 */
	}
}
