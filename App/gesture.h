#ifndef __GESTURE_H
#define __GESTURE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	GESTURE_KEY_NONE = 0,
	GESTURE_KEY_SINGLE_CLICK,
	GESTURE_KEY_DOUBLE_CLICK,
	GESTURE_KEY_LONG_PRESS,
} gesture_key_event_t;

void gesture_task(void *arg);
bool gesture_key_event_get(gesture_key_event_t *evt);

#endif
