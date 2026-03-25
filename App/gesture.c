#include "gesture.h"
#include "key.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

void gesture_task(void *arg)
{
	ButtonState buttonState = IDLE_STATE;
	(void)arg;

	for (;;) {
		ButtonState evt = button_scan(false, &buttonState);

		switch (evt) {
		case SHORT_PRESS_STATE:
			DBG_PRINTF("[key] short press\n");
			break;
		case LONG_PRESS_STATE:
			DBG_PRINTF("[key] long press\n");
			break;
		case DOUBLE_PRESS_STATE:
			DBG_PRINTF("[key] double press\n");
			break;
		default:
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(5));   /* 5ms 轮询，配合 key 10ms 扫描更灵敏 */
	}
}
