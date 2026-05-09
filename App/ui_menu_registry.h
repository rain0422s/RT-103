#ifndef __UI_MENU_REGISTRY_H
#define __UI_MENU_REGISTRY_H

#include "oled.h"
#include <stdbool.h>

typedef bool (*ui_menu_register_fn_t)(const char *name, void (*on_long_press)(u8g2_t *pu8g2));

void ui_menu_registry_register_all(ui_menu_register_fn_t reg_fn);

#endif
