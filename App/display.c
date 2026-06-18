/**
 * @file display.c
 * U8G2 OLED UI
 */
#include "display.h"
#include "storage.h"
#include "sensor.h"
#include "gesture.h"
#include "lfs_port.h"
#include "ui_menu_registry.h"
#include "rtc_clock.h"
#include "task.h"
#include <stdio.h>

extern const uint8_t u8g2_font_6x12_tf[];

static u8g2_t u8g2;

typedef struct {
        uint8_t id;
        uint8_t press;
        uint8_t long_press;
        uint8_t double_press;
        uint8_t update_flag;
} key_msg_t;

typedef enum {
        UI_MODE_CLOCK = 0,
        UI_MODE_MENU,
} ui_mode_t;

typedef struct ui_menu_item {
        const char *str;
        void (*on_long_press)(u8g2_t *pu8g2);
        uint8_t mode_mask;
        struct ui_menu_item *next;
} ui_menu_item_t;

static bool ui_menu_register(const char *name, void (*on_long_press)(u8g2_t *pu8g2),
                             uint8_t mode_mask);
static int ui_menu_count(void);
static const ui_menu_item_t *ui_menu_get_by_index(int idx);
static void ui_menu_clamp_selection(void);
static void ui_update_frame_target(u8g2_t *pu8g2);

#define UI_MENU_MAX_ITEMS 16
static ui_menu_item_t s_menu_nodes[UI_MENU_MAX_ITEMS];
static uint8_t s_menu_node_count;
static ui_menu_item_t *s_menu_head;
static ui_menu_item_t *s_menu_tail;
static bool s_menu_defaults_registered;

static short frame_len, frame_len_trg;
static short frame_y, frame_y_trg;
static short list_scroll_y, list_scroll_y_trg;
static short x = 0;
static signed char ui_select = 0;
static bool ui_flag = true;
static ui_mode_t s_ui_mode = UI_MODE_CLOCK;
static short s_view_x;
static short s_view_x_trg;
static short s_line_h = 18;
static short s_frame_h = 20;
static short s_text_x = 2;
static short s_text_y0 = 13;
#define UI_BOOT_IMAGE_MS 700
#define UI_CLOCK_RAW_UNSET 0xFFFFFFFFu

static key_msg_t key_msg = {0};
static bool s_force_full_refresh = true;
static eeprom_config_t s_ui_config;
static uint32_t s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
static volatile bool s_shutdown_prompt_show;
static volatile bool s_shutdown_prompt_confirmed;
static volatile uint8_t s_shutdown_prompt_progress_pct;

static short abs_s(short i)
{
        return (i < 0) ? (short)(~(i - 1)) : i;
}

static void ui_config_defaults(eeprom_config_t *cfg)
{
        if (cfg == NULL)
                return;
        *cfg = (eeprom_config_t){
                .magic = EEPROM_MAGIC,
                .version = EEPROM_CONFIG_VERSION,
                .ui_select = 0,
                .rtc_calib_sec_per_day = 0,
                .rtc_calib_anchor_raw = 0,
                .rtc_uptime_seconds = 0,
                .clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME,
        };
}

static bool ui_clock_mode_is_rtc(void)
{
        return s_ui_config.clock_display_mode == CLOCK_DISPLAY_MODE_RTC;
}

static void ui_config_sanitize(eeprom_config_t *cfg)
{
        if (cfg == NULL)
                return;
        if (cfg->clock_display_mode != CLOCK_DISPLAY_MODE_RTC &&
            cfg->clock_display_mode != CLOCK_DISPLAY_MODE_UPTIME) {
                cfg->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
        }
}

static const char *ui_clock_mode_name(uint8_t mode)
{
        return (mode == CLOCK_DISPLAY_MODE_RTC) ? "RTC Time" : "Uptime";
}

static void ui_config_load(void)
{
        ui_config_defaults(&s_ui_config);
        (void)storage_load_config(&s_ui_config);
        s_ui_config.magic = EEPROM_MAGIC;
        s_ui_config.version = EEPROM_CONFIG_VERSION;
        ui_config_sanitize(&s_ui_config);
        rtc_clock_uptime_sync(&s_ui_config);
}

static void ui_config_save(void)
{
        s_ui_config.magic = EEPROM_MAGIC;
        s_ui_config.version = EEPROM_CONFIG_VERSION;
        ui_config_sanitize(&s_ui_config);
        (void)storage_save_config(&s_ui_config);
}

void display_wait_double_click_exit(void)
{
        for (;;) {
                gesture_key_event_t evt = GESTURE_KEY_NONE;
                while (gesture_key_event_get(&evt)) {
                        if (evt == GESTURE_KEY_DOUBLE_CLICK)
                                return;
                }
                delay_ms(20);
        }
}

void display_show_view_until_double_click(u8g2_t *pu8g2, display_draw_once_fn_t draw_once)
{
        if (pu8g2 == NULL || draw_once == NULL)
                return;
        draw_once(pu8g2);
        display_wait_double_click_exit();
}

void display_show_text_feedback(u8g2_t *pu8g2, const char *line1, const char *line2, uint16_t hold_ms)
{
        if (pu8g2 == NULL)
                return;
        u8g2_ClearBuffer(pu8g2);
        if (line1 != NULL)
                u8g2_DrawStr(pu8g2, 8, 24, line1);
        if (line2 != NULL)
                u8g2_DrawStr(pu8g2, 8, 44, line2);
        u8g2_SendBuffer(pu8g2);
        if (hold_ms == 0U) {
                display_wait_double_click_exit();
                return;
        }
        delay_ms(hold_ms);
}

static void display_draw_clock_mode_editor(u8g2_t *pu8g2, uint8_t mode)
{
        const char *name = ui_clock_mode_name(mode);
        uint8_t text_w;

        u8g2_ClearBuffer(pu8g2);
        u8g2_DrawStr(pu8g2, 31, 14, "Clock Mode");
        text_w = (uint8_t)u8g2_GetStrWidth(pu8g2, name);
        u8g2_DrawStr(pu8g2, (short)((CONFIG_SCREEN_WIDTH - text_w) / 2), 38, name);
        u8g2_SendBuffer(pu8g2);
}

void display_action_clock_mode(u8g2_t *pu8g2)
{
        uint8_t mode;

        if (pu8g2 == NULL)
                return;
        mode = ui_clock_mode_is_rtc() ? CLOCK_DISPLAY_MODE_RTC : CLOCK_DISPLAY_MODE_UPTIME;

        for (;;) {
                gesture_key_event_t evt = GESTURE_KEY_NONE;

                display_draw_clock_mode_editor(pu8g2, mode);
                while (gesture_key_event_get(&evt)) {
                        if (evt == GESTURE_KEY_SINGLE_CLICK || evt == GESTURE_KEY_LONG_PRESS) {
                                mode = (mode == CLOCK_DISPLAY_MODE_RTC) ?
                                       CLOCK_DISPLAY_MODE_UPTIME : CLOCK_DISPLAY_MODE_RTC;
                        } else if (evt == GESTURE_KEY_DOUBLE_CLICK) {
                                s_ui_config.clock_display_mode = mode;
                                ui_config_save();
                                ui_menu_clamp_selection();
                                ui_update_frame_target(pu8g2);
                                s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
                                display_show_text_feedback(pu8g2, "Clock Mode",
                                                           ui_clock_mode_name(mode), 600);
                                s_force_full_refresh = true;
                                return;
                        }
                }
                delay_ms(60);
        }
}

void display_action_uptime_reset(u8g2_t *pu8g2)
{
        if (pu8g2 == NULL)
                return;
        rtc_clock_uptime_set(&s_ui_config, 0);
        ui_config_save();
        s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
        display_show_text_feedback(pu8g2, "Uptime", "Reset", 600);
        s_force_full_refresh = true;
}

static void display_draw_time_editor(u8g2_t *pu8g2, uint8_t hour, uint8_t minute,
                                     uint8_t second, uint8_t field)
{
        char buf[16];
        static const short field_x[] = {24, 48, 72};

        u8g2_ClearBuffer(pu8g2);
        u8g2_DrawStr(pu8g2, 35, 14, "Time Set");
        (void)snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hour, minute, second);
        u8g2_DrawStr(pu8g2, 24, 38, buf);
        if (field < 3u)
                u8g2_DrawFrame(pu8g2, field_x[field], 24, 18, 18);
        u8g2_SendBuffer(pu8g2);
}

void display_action_time_set(u8g2_t *pu8g2)
{
        rtc_clock_time_t now;
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        uint8_t field = 0;

        if (pu8g2 == NULL)
                return;
        (void)rtc_clock_poll();
        if (!rtc_clock_is_ready()) {
                display_show_text_feedback(pu8g2, "RTC",
                                           rtc_clock_is_starting() ? "Starting" : "Unavailable",
                                           900);
                return;
        }

        now = rtc_clock_get_time(&s_ui_config);
        hour = now.valid ? now.hour : 0;
        minute = now.valid ? now.minute : 0;
        second = now.valid ? now.second : 0;

        for (;;) {
                gesture_key_event_t evt = GESTURE_KEY_NONE;

                display_draw_time_editor(pu8g2, hour, minute, second, field);
                while (gesture_key_event_get(&evt)) {
                        if (evt == GESTURE_KEY_SINGLE_CLICK) {
                                if (field == 0)
                                        hour = (uint8_t)((hour + 1u) % 24u);
                                else if (field == 1)
                                        minute = (uint8_t)((minute + 1u) % 60u);
                                else
                                        second = (uint8_t)((second + 1u) % 60u);
                        } else if (evt == GESTURE_KEY_LONG_PRESS) {
                                field = (uint8_t)((field + 1u) % 3u);
                        } else if (evt == GESTURE_KEY_DOUBLE_CLICK) {
                                if (rtc_clock_set_time(hour, minute, second)) {
                                        s_ui_config.rtc_calib_anchor_raw = rtc_clock_get_raw_seconds();
                                        ui_config_save();
                                        display_show_text_feedback(pu8g2, "Time Set", "Saved", 600);
                                } else {
                                        display_show_text_feedback(pu8g2, "Time Set", "Failed", 900);
                                }
                                s_force_full_refresh = true;
                                return;
                        }
                }
                delay_ms(60);
        }
}

static void display_draw_calib_editor(u8g2_t *pu8g2, int8_t value)
{
        char buf[18];

        u8g2_ClearBuffer(pu8g2);
        u8g2_DrawStr(pu8g2, 32, 14, "RTC Calib");
        (void)snprintf(buf, sizeof(buf), "%+d s/day", (int)value);
        u8g2_DrawStr(pu8g2, 34, 38, buf);
        u8g2_SendBuffer(pu8g2);
}

void display_action_rtc_calib(u8g2_t *pu8g2)
{
        int8_t value;
        int8_t dir;

        if (pu8g2 == NULL)
                return;

        value = rtc_clock_calib_get(&s_ui_config);
        dir = (value < 0) ? -1 : 1;

        for (;;) {
                gesture_key_event_t evt = GESTURE_KEY_NONE;

                display_draw_calib_editor(pu8g2, value);
                while (gesture_key_event_get(&evt)) {
                        if (evt == GESTURE_KEY_SINGLE_CLICK) {
                                value = (int8_t)(value + dir);
                                if (value > 30)
                                        value = 0;
                                else if (value < -30)
                                        value = 0;
                        } else if (evt == GESTURE_KEY_LONG_PRESS) {
                                if (value == 0)
                                        dir = (int8_t)-dir;
                                else {
                                        value = (int8_t)-value;
                                        dir = (value < 0) ? -1 : 1;
                                }
                        } else if (evt == GESTURE_KEY_DOUBLE_CLICK) {
                                rtc_clock_calib_set(&s_ui_config, value);
                                ui_config_save();
                                display_show_text_feedback(pu8g2, "RTC Calib", "Saved", 600);
                                s_force_full_refresh = true;
                                return;
                        }
                }
                delay_ms(60);
        }
}

static bool ui_menu_register(const char *name, void (*on_long_press)(u8g2_t *pu8g2),
                             uint8_t mode_mask)
{
        ui_menu_item_t *node;

        if (name == NULL || s_menu_node_count >= UI_MENU_MAX_ITEMS)
                return false;
        node = &s_menu_nodes[s_menu_node_count];
        node->str = name;
        node->on_long_press = on_long_press;
        node->mode_mask = (mode_mask == 0u) ? UI_MENU_MODE_ALL : mode_mask;
        node->next = NULL;

        if (s_menu_head == NULL)
                s_menu_head = node;
        else
                s_menu_tail->next = node;
        s_menu_tail = node;
        s_menu_node_count++;
        return true;
}

bool display_menu_register_item(const char *name, void (*on_long_press)(u8g2_t *pu8g2))
{
        return display_menu_register_mode_item(name, on_long_press, UI_MENU_MODE_ALL);
}

bool display_menu_register_mode_item(const char *name, void (*on_long_press)(u8g2_t *pu8g2),
                                     uint8_t mode_mask)
{
        return ui_menu_register(name, on_long_press, mode_mask);
}

static uint8_t ui_current_menu_mode_mask(void)
{
        return ui_clock_mode_is_rtc() ? UI_MENU_MODE_RTC : UI_MENU_MODE_UPTIME;
}

static bool ui_menu_item_visible(const ui_menu_item_t *item)
{
        return item != NULL && (item->mode_mask & ui_current_menu_mode_mask()) != 0u;
}

static int ui_menu_count(void)
{
        const ui_menu_item_t *it = s_menu_head;
        int count = 0;

        while (it != NULL) {
                if (ui_menu_item_visible(it))
                        count++;
                it = it->next;
        }
        return count;
}

static const ui_menu_item_t *ui_menu_get_by_index(int idx)
{
        const ui_menu_item_t *it = s_menu_head;
        int i = 0;

        if (idx < 0)
                return NULL;
        while (it != NULL) {
                if (!ui_menu_item_visible(it)) {
                        it = it->next;
                        continue;
                }
                if (i == idx)
                        return it;
                it = it->next;
                i++;
        }
        return NULL;
}

static void ui_menu_clamp_selection(void)
{
        const int list_len = ui_menu_count();

        if (list_len <= 0) {
                ui_select = 0;
                ui_flag = true;
                return;
        }
        if (ui_select < 0)
                ui_select = 0;
        if (ui_select >= list_len)
                ui_select = (signed char)(list_len - 1);
        if (ui_select <= 0)
                ui_flag = true;
}

static void ui_execute_selected_action(u8g2_t *pu8g2)
{
        const ui_menu_item_t *item = ui_menu_get_by_index(ui_select);

        if (item == NULL)
                return;
        if (item->on_long_press != NULL)
                item->on_long_press(pu8g2);
        s_force_full_refresh = true;
}

static void key_scan(void)
{
        gesture_key_event_t evt = GESTURE_KEY_NONE;
        while (gesture_key_event_get(&evt)) {
                if (evt == GESTURE_KEY_SINGLE_CLICK || evt == GESTURE_KEY_LONG_PRESS ||
                    evt == GESTURE_KEY_DOUBLE_CLICK) {
                        key_msg.id = 0;
                        key_msg.press = (evt == GESTURE_KEY_SINGLE_CLICK);
                        key_msg.long_press = (evt == GESTURE_KEY_LONG_PRESS);
                        key_msg.double_press = (evt == GESTURE_KEY_DOUBLE_CLICK);
                        key_msg.update_flag = 1;
                }
        }
}

static uint8_t ui_run(short *a, short *a_trg, uint8_t step, uint8_t slow_cnt)
{
        const short diff = (short)(*a_trg - *a);
        const short distance = abs_s(diff);
        short temp;

        if (distance == 0)
                return 0;
        temp = (distance > slow_cnt) ? step : 1;
        if (temp > distance)
                temp = distance;
        if (diff > 0)
                *a += temp;
        else
                *a -= temp;
        return 1;
}

/** Vertical scroll indicator: opposite side of list text (right edge). */
static void ui_draw_list_scroll_indicator(u8g2_t *pu8g2, short offset_x)
{
        const int list_len = ui_menu_count();
        short visible_rows = (short)((CONFIG_SCREEN_HEIGHT - 2) / s_line_h);
        const short track_x = (short)(CONFIG_SCREEN_WIDTH - 5 + offset_x);
        const short track_w = 3;
        const short track_top = 2;
        const short track_h = (short)(CONFIG_SCREEN_HEIGHT - 4);
        short thumb_h;
        short thumb_y;

        if (list_len <= 0)
                return;
        if (visible_rows < 1)
                visible_rows = 1;

        u8g2_DrawFrame(pu8g2, track_x, track_top, track_w, track_h);

        if (list_len <= visible_rows) {
                u8g2_DrawBox(pu8g2, (short)(track_x + 1), (short)(track_top + 1),
                             (short)(track_w - 2), (short)(track_h - 2));
                return;
        }

        thumb_h = (short)((visible_rows * track_h) / list_len);
        if (thumb_h < 4)
                thumb_h = 4;
        if (thumb_h > track_h - 2)
                thumb_h = (short)(track_h - 2);

        {
                const short scroll_max = (short)((list_len - visible_rows) * s_line_h);
                const short inner = (short)(track_h - 2);
                short sy = list_scroll_y;

                thumb_y = (short)(track_top + 1);
                if (scroll_max > 0) {
                        if (sy < 0)
                                sy = 0;
                        else if (sy > scroll_max)
                                sy = scroll_max;
                        thumb_y += (short)(((long)(inner - thumb_h) * sy + scroll_max / 2) / scroll_max);
                }
                {
                        const short max_y = (short)(track_top + 1 + inner - thumb_h);

                        if (thumb_y > max_y)
                                thumb_y = max_y;
                }
        }

        u8g2_DrawBox(pu8g2, (short)(track_x + 1), thumb_y, (short)(track_w - 2), thumb_h);
}

static void ui_update_frame_target(u8g2_t *pu8g2)
{
        const short list_len = (short)ui_menu_count();
        short visible_rows = (short)((CONFIG_SCREEN_HEIGHT - 2) / s_line_h);
        short top_index = 0;
        const ui_menu_item_t *item = ui_menu_get_by_index(ui_select);
        const char *selected_text = (item != NULL) ? item->str : "";
        const uint8_t text_w = (uint8_t)u8g2_GetStrWidth(pu8g2, selected_text);

        if (list_len <= 0) {
                frame_y_trg = 0;
                frame_len_trg = 0;
                list_scroll_y_trg = 0;
                return;
        }
        if (visible_rows < 1) {
                visible_rows = 1;
        }
        if (ui_select >= visible_rows) {
                top_index = (short)(ui_select - visible_rows + 1);
        }
        if (top_index > (list_len - visible_rows)) {
                top_index = (short)(list_len - visible_rows);
        }
        if (top_index < 0) {
                top_index = 0;
        }

        frame_y_trg = (short)(ui_select * s_line_h);
        frame_len_trg = (short)(text_w + 8); /* 4px padding on each side */
        list_scroll_y_trg = (short)(top_index * s_line_h);
}

#define UI_CLOCK_DIGIT_W 19
#define UI_CLOCK_DIGIT_H 46
#define UI_CLOCK_SEG_T   4
#define UI_CLOCK_GAP     3
#define UI_CLOCK_LEFT_X  2
#define UI_CLOCK_TOP_Y   9
#define UI_CLOCK_SIDE_X  100
#define UI_CLOCK_SEG_A   0x01u
#define UI_CLOCK_SEG_B   0x02u
#define UI_CLOCK_SEG_C   0x04u
#define UI_CLOCK_SEG_D   0x08u
#define UI_CLOCK_SEG_E   0x10u
#define UI_CLOCK_SEG_F   0x20u
#define UI_CLOCK_SEG_G   0x40u

static void ui_draw_box_clipped(u8g2_t *pu8g2, short x, short y, short w, short h)
{
        if (w <= 0 || h <= 0)
                return;
        if (x < 0) {
                w = (short)(w + x);
                x = 0;
        }
        if (y < 0) {
                h = (short)(h + y);
                y = 0;
        }
        if (x >= CONFIG_SCREEN_WIDTH || y >= CONFIG_SCREEN_HEIGHT)
                return;
        if ((short)(x + w) > CONFIG_SCREEN_WIDTH)
                w = (short)(CONFIG_SCREEN_WIDTH - x);
        if ((short)(y + h) > CONFIG_SCREEN_HEIGHT)
                h = (short)(CONFIG_SCREEN_HEIGHT - y);
        if (w <= 0 || h <= 0)
                return;
        u8g2_DrawBox(pu8g2, (u8g2_uint_t)x, (u8g2_uint_t)y,
                     (u8g2_uint_t)w, (u8g2_uint_t)h);
}

static void ui_draw_str_visible(u8g2_t *pu8g2, short x, short y, const char *str)
{
        if (str == NULL || x < 0 || x >= CONFIG_SCREEN_WIDTH ||
            y <= 0 || y > CONFIG_SCREEN_HEIGHT) {
                return;
        }
        u8g2_DrawStr(pu8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, str);
}

static uint8_t ui_7seg_mask(uint8_t digit)
{
        static const uint8_t masks[10] = {
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_B | UI_CLOCK_SEG_C |
                UI_CLOCK_SEG_D | UI_CLOCK_SEG_E | UI_CLOCK_SEG_F,
                UI_CLOCK_SEG_B | UI_CLOCK_SEG_C,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_B | UI_CLOCK_SEG_G |
                UI_CLOCK_SEG_E | UI_CLOCK_SEG_D,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_B | UI_CLOCK_SEG_C |
                UI_CLOCK_SEG_D | UI_CLOCK_SEG_G,
                UI_CLOCK_SEG_F | UI_CLOCK_SEG_G | UI_CLOCK_SEG_B |
                UI_CLOCK_SEG_C,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_F | UI_CLOCK_SEG_G |
                UI_CLOCK_SEG_C | UI_CLOCK_SEG_D,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_F | UI_CLOCK_SEG_E |
                UI_CLOCK_SEG_D | UI_CLOCK_SEG_C | UI_CLOCK_SEG_G,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_B | UI_CLOCK_SEG_C,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_B | UI_CLOCK_SEG_C |
                UI_CLOCK_SEG_D | UI_CLOCK_SEG_E | UI_CLOCK_SEG_F |
                UI_CLOCK_SEG_G,
                UI_CLOCK_SEG_A | UI_CLOCK_SEG_B | UI_CLOCK_SEG_C |
                UI_CLOCK_SEG_D | UI_CLOCK_SEG_F | UI_CLOCK_SEG_G,
        };

        return masks[digit % 10u];
}

static void ui_draw_7seg_digit(u8g2_t *pu8g2, short x, short y, uint8_t digit)
{
        const short w = UI_CLOCK_DIGIT_W;
        const short h = UI_CLOCK_DIGIT_H;
        const short t = UI_CLOCK_SEG_T;
        const short half = (short)(h / 2);
        const uint8_t mask = ui_7seg_mask(digit);

        if ((mask & UI_CLOCK_SEG_A) != 0u)
                ui_draw_box_clipped(pu8g2, (short)(x + t), y, (short)(w - 2 * t), t);
        if ((mask & UI_CLOCK_SEG_B) != 0u)
                ui_draw_box_clipped(pu8g2, (short)(x + w - t), (short)(y + t), t,
                                    (short)(half - t));
        if ((mask & UI_CLOCK_SEG_C) != 0u)
                ui_draw_box_clipped(pu8g2, (short)(x + w - t), (short)(y + half + 1), t,
                                    (short)(h - half - t - 1));
        if ((mask & UI_CLOCK_SEG_D) != 0u)
                ui_draw_box_clipped(pu8g2, (short)(x + t), (short)(y + h - t),
                                    (short)(w - 2 * t), t);
        if ((mask & UI_CLOCK_SEG_E) != 0u)
                ui_draw_box_clipped(pu8g2, x, (short)(y + half + 1), t,
                                    (short)(h - half - t - 1));
        if ((mask & UI_CLOCK_SEG_F) != 0u)
                ui_draw_box_clipped(pu8g2, x, (short)(y + t), t, (short)(half - t));
        if ((mask & UI_CLOCK_SEG_G) != 0u)
                ui_draw_box_clipped(pu8g2, (short)(x + t), (short)(y + half),
                                    (short)(w - 2 * t), t);
}

static void ui_draw_7seg_colon(u8g2_t *pu8g2, short x, short y)
{
        ui_draw_box_clipped(pu8g2, x, (short)(y + 16), 4, 4);
        ui_draw_box_clipped(pu8g2, x, (short)(y + 29), 4, 4);
}

static void ui_draw_7seg_time(u8g2_t *pu8g2, short x, short y, uint8_t hour, uint8_t minute)
{
        const short step = (short)(UI_CLOCK_DIGIT_W + UI_CLOCK_GAP);
        short dx = x;

        ui_draw_7seg_digit(pu8g2, dx, y, (uint8_t)(hour / 10u));
        dx = (short)(dx + step);
        ui_draw_7seg_digit(pu8g2, dx, y, (uint8_t)(hour % 10u));
        dx = (short)(dx + UI_CLOCK_DIGIT_W + 3);
        ui_draw_7seg_colon(pu8g2, dx, y);
        dx = (short)(dx + 8);
        ui_draw_7seg_digit(pu8g2, dx, y, (uint8_t)(minute / 10u));
        dx = (short)(dx + step);
        ui_draw_7seg_digit(pu8g2, dx, y, (uint8_t)(minute % 10u));
}

static void ui_draw_clock_side_panel(u8g2_t *pu8g2, short offset_x, bool rtc_mode,
                                     uint8_t second, bool valid, uint32_t uptime_days)
{
        const short x0 = (short)(offset_x + UI_CLOCK_SIDE_X);
        char buf[8];

        ui_draw_str_visible(pu8g2, x0, 9, "MODE");
        ui_draw_str_visible(pu8g2, x0, 20, rtc_mode ? "RTC" : "UP");
        ui_draw_box_clipped(pu8g2, x0, 24, 26, 1);

        ui_draw_str_visible(pu8g2, x0, 35, "SEC");
        (void)snprintf(buf, sizeof(buf), "%02u", second);
        ui_draw_str_visible(pu8g2, (short)(x0 + 8), 47, buf);
        ui_draw_box_clipped(pu8g2, x0, 50, 26, 1);

        if (rtc_mode) {
                if (!rtc_clock_is_ready())
                        ui_draw_str_visible(pu8g2, x0, 62,
                                            rtc_clock_is_starting() ? "WAIT" : "FAIL");
                else
                        ui_draw_str_visible(pu8g2, x0, 62, valid ? "SET" : "NSET");
        } else if (uptime_days > 0u) {
                if (uptime_days > 99u)
                        (void)snprintf(buf, sizeof(buf), "D++");
                else
                        (void)snprintf(buf, sizeof(buf), "D%lu", (unsigned long)uptime_days);
                ui_draw_str_visible(pu8g2, x0, 62, buf);
        } else {
                ui_draw_str_visible(pu8g2, x0, 62, "RUN");
        }
}

static void ui_draw_clock(u8g2_t *pu8g2, short offset_x)
{
        const bool rtc_mode = ui_clock_mode_is_rtc();
        rtc_clock_time_t t = {0};
        uint32_t uptime_seconds = 0;
        uint32_t total_hours = 0;
        uint32_t uptime_days = 0;
        uint8_t display_hour = 0;
        uint8_t display_minute = 0;
        uint8_t display_second = 0;

        if (rtc_mode) {
                t = rtc_clock_get_time(&s_ui_config);
                if (t.valid) {
                        display_hour = t.hour;
                        display_minute = t.minute;
                        display_second = t.second;
                }
        } else {
                uptime_seconds = rtc_clock_get_uptime_seconds(&s_ui_config);
                total_hours = uptime_seconds / 3600u;
                uptime_days = uptime_seconds / 86400u;
                display_hour = (uint8_t)(total_hours % 100u);
                display_minute = (uint8_t)((uptime_seconds / 60u) % 60u);
                display_second = (uint8_t)(uptime_seconds % 60u);
                t.valid = true;
        }

        ui_draw_7seg_time(pu8g2, (short)(offset_x + UI_CLOCK_LEFT_X), UI_CLOCK_TOP_Y,
                          display_hour, display_minute);
        ui_draw_clock_side_panel(pu8g2, offset_x, rtc_mode, display_second,
                                 t.valid, uptime_days);

        if (t.valid && rtc_mode)
                s_last_clock_raw_seconds = rtc_clock_get_raw_seconds();
        else if (t.valid)
                s_last_clock_raw_seconds = rtc_clock_get_uptime_seconds(&s_ui_config);
        else
                s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
}

static void ui_draw_menu(u8g2_t *pu8g2, short offset_x)
{
        const int list_len = ui_menu_count();
        const ui_menu_item_t *it = s_menu_head;
        int i = 0;

        while (it != NULL && i < list_len) {
                if (!ui_menu_item_visible(it)) {
                        it = it->next;
                        continue;
                }
                const short yy = (short)(s_text_y0 + i * s_line_h - list_scroll_y);
                const char *line = it->str;
                u8g2_DrawStr(pu8g2, (short)(s_text_x + offset_x), yy, line);
                it = it->next;
                i++;
        }
        u8g2_DrawRFrame(pu8g2, (short)(x + offset_x), (short)(frame_y - list_scroll_y),
                        frame_len, s_frame_h, 3);
        ui_draw_list_scroll_indicator(pu8g2, offset_x);
}

static bool ui_update_animation(void)
{
        const bool anim_view = ui_run(&s_view_x, &s_view_x_trg, 12, 3) != 0;
        const bool anim_y = ui_run(&frame_y, &frame_y_trg, 5, 4) != 0;
        const bool anim_len = ui_run(&frame_len, &frame_len_trg, 10, 5) != 0;
        const bool anim_scroll = ui_run(&list_scroll_y, &list_scroll_y_trg, 5, 4) != 0;

        return anim_view || anim_y || anim_len || anim_scroll;
}

static bool ui_clock_refresh_due(void)
{
        uint32_t raw_seconds;

        if (s_ui_mode != UI_MODE_CLOCK || s_view_x != 0 || s_view_x_trg != 0)
                return false;
        if (ui_clock_mode_is_rtc()) {
                if (!rtc_clock_is_ready() || !rtc_clock_time_is_set())
                        return false;
                raw_seconds = rtc_clock_get_raw_seconds();
        } else {
                raw_seconds = rtc_clock_get_uptime_seconds(&s_ui_config);
        }
        if (raw_seconds == s_last_clock_raw_seconds)
                return false;
        s_last_clock_raw_seconds = raw_seconds;
        return true;
}

static bool ui_show(u8g2_t *pu8g2, bool force_full)
{
        bool animating;
        const bool clock_due = ui_clock_refresh_due();

        animating = ui_update_animation();
        if (!force_full && !animating && !clock_due)
                return false;
        u8g2_ClearBuffer(pu8g2);
        ui_draw_clock(pu8g2, (short)(-s_view_x));
        ui_draw_menu(pu8g2, (short)(CONFIG_SCREEN_WIDTH - s_view_x));
        u8g2_SendBuffer(pu8g2);
        return animating;
}

static bool ui_proc(u8g2_t *pu8g2)
{
        const int list_len = ui_menu_count();
        bool got_ui_event = false;
        if (list_len <= 0)
                return ui_show(pu8g2, true);

        if (key_msg.update_flag && key_msg.double_press) {
                key_msg.update_flag = 0;
                key_msg.double_press = 0;
                key_msg.long_press = 0;
                key_msg.press = 0;
                got_ui_event = true;
                if (s_ui_mode == UI_MODE_CLOCK) {
                        s_ui_mode = UI_MODE_MENU;
                        s_view_x_trg = CONFIG_SCREEN_WIDTH;
                        ui_update_frame_target(pu8g2);
                } else {
                        s_ui_mode = UI_MODE_CLOCK;
                        s_view_x_trg = 0;
                }
        } else if (s_ui_mode == UI_MODE_MENU && key_msg.update_flag && key_msg.long_press) {
                key_msg.update_flag = 0;
                key_msg.long_press = 0;
                key_msg.press = 0;
                key_msg.double_press = 0;
                got_ui_event = true;
                ui_execute_selected_action(pu8g2);
        } else if (s_ui_mode == UI_MODE_MENU && key_msg.update_flag && key_msg.press) {
                key_msg.update_flag = 0;
                key_msg.press = 0;
                key_msg.long_press = 0;
                key_msg.double_press = 0;
                got_ui_event = true;
                if (ui_flag) {
                        ui_select++;
                        if (ui_select == list_len - 1)
                                ui_flag = false;
                } else {
                        ui_select--;
                        if (ui_select == 0)
                                ui_flag = true;
                }
                ui_update_frame_target(pu8g2);
                s_ui_config.ui_select = ui_select;
                ui_config_save();
        } else if (key_msg.update_flag) {
                key_msg.update_flag = 0;
                key_msg.press = 0;
                key_msg.long_press = 0;
                key_msg.double_press = 0;
        }
        return ui_show(pu8g2, s_force_full_refresh || got_ui_event);
}

static bool loop1(u8g2_t *pu8g2) __attribute__((unused));
static bool loop1(u8g2_t *pu8g2)
{
        static const u8g2_cb_t *s_last_rot = U8G2_R2;
        const sensor_6d_dir_t d = sensor_get_6d_dir();
        const u8g2_cb_t *rot = s_last_rot;
        bool active = false;

        /* Only switch on up/down direction (forward/backward). */
        if (d == SENSOR_6D_DIR_BACKWARD)
                rot = U8G2_R2;
        else if (d == SENSOR_6D_DIR_FORWARD)
                rot = U8G2_R0;

        if (rot != s_last_rot) {
                s_last_rot = rot;
                u8g2_SetDisplayRotation(pu8g2, rot);
                s_force_full_refresh = true;
                active = true;
        }
        if (rtc_clock_poll()) {
                s_force_full_refresh = true;
                active = true;
        }
        key_scan();
        {
                const bool animating = ui_proc(pu8g2);
                if (!animating) {
                        s_force_full_refresh = false;
                } else {
                        active = true;
                }
        }
        return active;
}

void display_show_shutdown_prompt(bool show, bool confirmed, uint8_t progress_pct)
{
        s_shutdown_prompt_show = show;
        s_shutdown_prompt_confirmed = confirmed;
        s_shutdown_prompt_progress_pct = (progress_pct > 100u) ? 100u : progress_pct;
}

void ui_test(u8g2_t *pu8g2)
{
#if OLED_RAW_TEST_MODE
        oled_raw_white_test(pu8g2);
#else
        u8g2Init(pu8g2);
        /* Use larger imported font from local u8g2 source. */
        u8g2_SetFont(pu8g2, u8g2_font_6x12_tf);
        {
                const short ascent = (short)u8g2_GetAscent(pu8g2);
                const short descent = (short)u8g2_GetDescent(pu8g2);
                const short font_h = (short)(ascent - descent);
                /* Keep font size fixed and scroll list when items exceed view. */
                s_line_h = (short)(font_h + 3);
                s_frame_h = (short)(font_h + 4);
                s_text_x = 4;
                s_text_y0 = (short)(2 + ascent);
        }
        oled_boot_splash_show(pu8g2);
        delay_ms(UI_BOOT_IMAGE_MS);
        frame_y = frame_y_trg = (short)(ui_select * s_line_h);
        ui_update_frame_target(pu8g2);
        frame_len = frame_len_trg;
        list_scroll_y = list_scroll_y_trg;
        s_ui_mode = UI_MODE_CLOCK;
        s_view_x = 0;
        s_view_x_trg = 0;
        s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
#endif
}

void ui_task(void *arg)
{
        int list_len;
        uint32_t stack_log_tick_ms = HAL_GetTick();
        int8_t init_sel = 0;
        (void)arg;

        storage_eeprom_init();
        ui_config_load();
        init_sel = s_ui_config.ui_select;

        if (!s_menu_defaults_registered) {
                ui_menu_registry_register_all(ui_menu_register);
                s_menu_defaults_registered = true;
        }
        list_len = ui_menu_count();
        if (list_len <= 0) {
                for (;;) {
                        delay_ms(200);
                }
        }
        if (init_sel < 0)
                init_sel = 0;
        if (init_sel >= list_len)
                init_sel = list_len - 1;
        ui_select = init_sel;
        ui_flag = (init_sel == 0);
        ui_test(&u8g2);
#if OLED_RAW_TEST_MODE
        for (;;) {
                delay_ms(1000);
        }
#else
        /* Gate UI startup by LittleFS mount state:
         * keep showing boot splash until filesystem is mounted. */
        while (lfs_get() == NULL) {
                oled_boot_splash_show(&u8g2);
                delay_ms(200);
        }
        DBG_PRINTF("[ui_task] stack watermark=%lu words\n",
                   (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        for (;;) {
                if (s_shutdown_prompt_show) {
                        const uint8_t pct = s_shutdown_prompt_progress_pct;
                        const uint8_t bar_w = (uint8_t)((108u * pct) / 100u);
                        u8g2_ClearBuffer(&u8g2);
                        u8g2_DrawStr(&u8g2, 10, 22, "Power key pressed");
                        if (s_shutdown_prompt_confirmed)
                                u8g2_DrawStr(&u8g2, 10, 42, "Shutting down...");
                        else
                                u8g2_DrawStr(&u8g2, 10, 42, "Release to cancel");
                        u8g2_DrawFrame(&u8g2, 10, 50, 108, 10);
                        u8g2_DrawBox(&u8g2, 10, 50, bar_w, 10);
                        u8g2_SendBuffer(&u8g2);
                        delay_ms(60);
                        continue;
                }
                const bool active = loop1(&u8g2);
                if ((uint32_t)(HAL_GetTick() - stack_log_tick_ms) >= 5000U) {
                        stack_log_tick_ms = HAL_GetTick();
                        DBG_PRINTF("[ui_task] stack watermark=%lu words\n",
                                   (unsigned long)uxTaskGetStackHighWaterMark(NULL));
                }
                /* Lower refresh load while keeping responsive interaction. */
                delay_ms(active ? 10 : 100);
        }
#endif
}
