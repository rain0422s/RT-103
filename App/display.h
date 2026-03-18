#ifndef __DISPLAY_H
#define __DISPLAY_H
#include "display_config.h"
#include "utils.h"

/* U8G2_ENABLED 为 1 时：U8G2；否则：LVGL */
#ifdef U8G2_ENABLED
#include "oled.h"

void ui_test(u8g2_t *u8g2);
void ui_task(void *arg);
#else
#include "lv_init.h"
#include "lv_port_disp.h"
void demo_run(void);
#endif /* U8G2_ENABLED */
#endif