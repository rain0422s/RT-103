#include "ui_menu_registry.h"
#include "storage.h"
#include "display.h"
#include "oled.h"

static void ui_action_show_boot_image(u8g2_t *pu8g2)
{
        display_show_view_until_double_click(pu8g2, oled_boot_splash_show);
}

void ui_menu_registry_register_all(ui_menu_register_fn_t reg_fn)
{
        if (reg_fn == NULL)
                return;
        storage_menu_register_items(reg_fn);
        (void)reg_fn("list", ui_action_show_boot_image);
        (void)reg_fn("abc", ui_action_show_boot_image);
        (void)reg_fn("abcd", ui_action_show_boot_image);
        (void)reg_fn("temp", ui_action_show_boot_image);
        (void)reg_fn("humi", ui_action_show_boot_image);
        (void)reg_fn("press", ui_action_show_boot_image);
        (void)reg_fn("light", ui_action_show_boot_image);
}
