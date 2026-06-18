#ifndef __DISPLAY_H
#define __DISPLAY_H
#include "display_config.h"
#include "utils.h"
#include "ui_menu_mode.h"
#include "oled.h"
#include <stdbool.h>

typedef void (*display_draw_once_fn_t)(u8g2_t *pu8g2);

void ui_test(u8g2_t *u8g2);
void ui_task(void *arg);
void display_show_shutdown_prompt(bool show, bool confirmed, uint8_t progress_pct);
void display_wait_double_click_exit(void);
void display_show_view_until_double_click(u8g2_t *pu8g2, display_draw_once_fn_t draw_once);
void display_show_text_feedback(u8g2_t *pu8g2, const char *line1, const char *line2, uint16_t hold_ms);
bool display_menu_register_item(const char *name, void (*on_long_press)(u8g2_t *pu8g2));
bool display_menu_register_mode_item(const char *name, void (*on_long_press)(u8g2_t *pu8g2),
				     uint8_t mode_mask);
void display_action_clock_mode(u8g2_t *pu8g2);
void display_action_time_set(u8g2_t *pu8g2);
void display_action_rtc_calib(u8g2_t *pu8g2);
void display_action_uptime_reset(u8g2_t *pu8g2);

#endif
