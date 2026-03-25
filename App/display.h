#ifndef __DISPLAY_H
#define __DISPLAY_H
#include "display_config.h"
#include "utils.h"
#include "oled.h"

void ui_test(u8g2_t *u8g2);
void ui_task(void *arg);

#endif