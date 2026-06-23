#include "ui_menu_registry.h"
#include "storage.h"
#include "display.h"

void ui_menu_registry_register_all(ui_menu_register_fn_t reg_fn)
{
	if (reg_fn == NULL)
		return;
	storage_menu_register_items(reg_fn);
	(void)reg_fn("Battery Chart", display_action_battery_chart, UI_MENU_MODE_ALL);
	(void)reg_fn("Pose Calib", display_action_pose_calib, UI_MENU_MODE_ALL);
	(void)reg_fn("Clock Mode", display_action_clock_mode, UI_MENU_MODE_ALL);
	(void)reg_fn("Time Set", display_action_time_set, UI_MENU_MODE_RTC);
	(void)reg_fn("RTC Calib", display_action_rtc_calib, UI_MENU_MODE_RTC);
	(void)reg_fn("Uptime Reset", display_action_uptime_reset, UI_MENU_MODE_UPTIME);
}
