#include "ui_menu_registry.h"
#include "storage.h"
#include "display.h"
#include "oled.h"
#include <stddef.h>

static void ui_action_show_boot_image(u8g2_t *pu8g2)
{
	display_show_view_until_double_click(pu8g2, oled_boot_splash_show);
}

void ui_menu_registry_register_all(ui_menu_register_fn_t reg_fn)
{
	static const char *const splash_ids[] = {
		"list", "abc", "abcd", "temp", "humi", "press", "light",
	};

	if (reg_fn == NULL)
		return;
	storage_menu_register_items(reg_fn);
	(void)reg_fn("Clock Mode", display_action_clock_mode, UI_MENU_MODE_ALL);
	(void)reg_fn("Time Set", display_action_time_set, UI_MENU_MODE_RTC);
	(void)reg_fn("RTC Calib", display_action_rtc_calib, UI_MENU_MODE_RTC);
	(void)reg_fn("Uptime Reset", display_action_uptime_reset, UI_MENU_MODE_UPTIME);
	for (size_t i = 0; i < sizeof(splash_ids) / sizeof(splash_ids[0]); i++)
		(void)reg_fn(splash_ids[i], ui_action_show_boot_image, UI_MENU_MODE_ALL);
}
