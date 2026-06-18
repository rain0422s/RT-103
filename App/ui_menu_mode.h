#ifndef __UI_MENU_MODE_H
#define __UI_MENU_MODE_H

#include <stdint.h>

#define UI_MENU_MODE_RTC     ((uint8_t)0x01u)
#define UI_MENU_MODE_UPTIME  ((uint8_t)0x02u)
#define UI_MENU_MODE_ALL     ((uint8_t)(UI_MENU_MODE_RTC | UI_MENU_MODE_UPTIME))

#endif /* __UI_MENU_MODE_H */
