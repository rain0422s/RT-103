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
#include <string.h>

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
static void ui_draw_battery_icon(u8g2_t *pu8g2, short x, short y);

#define UI_MENU_MAX_ITEMS 16
static ui_menu_item_t s_menu_nodes[UI_MENU_MAX_ITEMS];
static uint8_t s_menu_node_count;
static ui_menu_item_t *s_menu_head;
static ui_menu_item_t *s_menu_tail;
static bool s_menu_defaults_registered;

static short frame_len, frame_len_trg;
static short frame_y, frame_y_trg;
static short list_scroll_y, list_scroll_y_trg;
static signed char ui_select = 0;
static bool ui_flag = true;
static ui_mode_t s_ui_mode = UI_MODE_CLOCK;
static short s_view_x;
static short s_view_x_trg;
static short s_menu_slide_x;
static short s_menu_slide_x_trg;
static signed char s_menu_prev_select = -1;
static int8_t s_menu_slide_dir = 1;
static short s_line_h = 18;
static short s_frame_h = 20;
static short s_text_x = 2;
static short s_text_y0 = 13;
#define UI_BOOT_IMAGE_MS 700
#define UI_CLOCK_RAW_UNSET 0xFFFFFFFFu
#define UI_TILT_MENU_COOLDOWN_MS 600U
#define UI_STORAGE_BOOT_WAIT_MS 3000U
#define UI_STORAGE_BOOT_POLL_MS 200U
#define UI_UPTIME_SAVE_INTERVAL_MS 300000U
#define UI_CONFIG_DEFER_SAVE_MS 3000U
#define UI_SENSOR_ACTIVE_HOLD_MS 800U

static key_msg_t key_msg = {0};
static bool s_force_full_refresh = true;
static eeprom_config_t s_ui_config;
static uint32_t s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
static volatile bool s_shutdown_prompt_show;
static volatile bool s_shutdown_prompt_confirmed;
static volatile uint8_t s_shutdown_prompt_progress_pct;
static uint32_t s_last_uptime_save_ms;
static uint32_t s_config_dirty_ms;
static bool s_config_dirty;
static uint32_t s_last_menu_tilt_ms;
static sensor_6d_dir_t s_last_menu_tilt_dir = SENSOR_6D_DIR_UNKNOWN;
static short s_rotation_anim_x;
static short s_rotation_anim_x_trg;
static bool s_rotation_anim_active;
static bool s_rotation_anim_out;
static const u8g2_cb_t *s_display_rot = U8G2_R2;
static const u8g2_cb_t *s_rotation_pending_rot;

static short abs_s(short i)
{
        return (i < 0) ? (short)(~(i - 1)) : i;
}

static uint32_t ui_tick_ms(void)
{
        return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void ui_config_defaults(eeprom_config_t *cfg)
{
        storage_config_set_defaults(cfg);
}

static bool ui_clock_mode_is_rtc(void)
{
        return s_ui_config.clock_display_mode == CLOCK_DISPLAY_MODE_RTC;
}

static void ui_config_sanitize(eeprom_config_t *cfg)
{
        storage_config_sanitize(cfg);
}

static const char *ui_clock_mode_name(uint8_t mode)
{
        return (mode == CLOCK_DISPLAY_MODE_RTC) ? "RTC Time" : "Uptime";
}

static void ui_config_load(void)
{
        ui_config_defaults(&s_ui_config);
        s_ui_config.magic = EEPROM_MAGIC;
        s_ui_config.version = EEPROM_CONFIG_VERSION;
        ui_config_sanitize(&s_ui_config);
        sensor_battery_apply_config(&s_ui_config);
        sensor_pose_apply_config(&s_ui_config);
        rtc_clock_uptime_sync(&s_ui_config);
        DBG_PRINTF("[ui_task] config defaults mode=%u sel=%d\n",
                   (unsigned int)s_ui_config.clock_display_mode,
                   (int)s_ui_config.ui_select);
}

void display_reload_persistent_config(void)
{
        eeprom_config_t cfg;
        uint32_t checkpoint_seconds = 0;

        ui_config_defaults(&cfg);
        (void)storage_load_config(&cfg);
        if (storage_uptime_checkpoint_load(&checkpoint_seconds) &&
            checkpoint_seconds > cfg.rtc_uptime_seconds) {
                cfg.rtc_uptime_seconds = checkpoint_seconds;
        }
        cfg.magic = EEPROM_MAGIC;
        cfg.version = EEPROM_CONFIG_VERSION;
        ui_config_sanitize(&cfg);
        s_ui_config = cfg;
        sensor_battery_apply_config(&s_ui_config);
        sensor_pose_apply_config(&s_ui_config);
        rtc_clock_uptime_sync(&s_ui_config);
        ui_select = s_ui_config.ui_select;
        ui_menu_clamp_selection();
        s_config_dirty = false;
        s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
        s_force_full_refresh = true;
        DBG_PRINTF("[ui_task] config reload mode=%u sel=%d uptime=%lu\n",
                   (unsigned int)s_ui_config.clock_display_mode,
                   (int)s_ui_config.ui_select,
                   (unsigned long)s_ui_config.rtc_uptime_seconds);
}

static void ui_config_save(void)
{
        s_ui_config.magic = EEPROM_MAGIC;
        s_ui_config.version = EEPROM_CONFIG_VERSION;
        ui_config_sanitize(&s_ui_config);
        (void)storage_save_config(&s_ui_config);
        sensor_battery_apply_config(&s_ui_config);
        sensor_pose_apply_config(&s_ui_config);
}

static void ui_config_mark_dirty(void)
{
        s_config_dirty = true;
        s_config_dirty_ms = ui_tick_ms();
}

static void ui_config_deferred_save(bool force)
{
        if (!s_config_dirty)
                return;
        if (!force &&
            (uint32_t)(ui_tick_ms() - s_config_dirty_ms) < UI_CONFIG_DEFER_SAVE_MS) {
                return;
        }
        ui_config_save();
        s_config_dirty = false;
}

static void ui_periodic_uptime_save(void)
{
        const uint32_t now_ms = ui_tick_ms();
        const uint32_t previous_uptime_seconds = s_ui_config.rtc_uptime_seconds;
        const uint32_t uptime_seconds = rtc_clock_get_uptime_seconds(&s_ui_config);

        if ((uint32_t)(now_ms - s_last_uptime_save_ms) < UI_UPTIME_SAVE_INTERVAL_MS)
                return;
        s_ui_config.rtc_uptime_seconds = uptime_seconds;
        s_ui_config.magic = EEPROM_MAGIC;
        s_ui_config.version = EEPROM_CONFIG_VERSION;
        ui_config_sanitize(&s_ui_config);
        if (storage_uptime_checkpoint_save(uptime_seconds)) {
                s_last_uptime_save_ms = now_ms;
                rtc_clock_uptime_sync(&s_ui_config);
                s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
        } else {
                s_ui_config.rtc_uptime_seconds = previous_uptime_seconds;
                s_last_uptime_save_ms = now_ms;
        }
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
        ui_draw_battery_icon(pu8g2, 2, 2);
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
        ui_draw_battery_icon(pu8g2, 2, 2);
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
                                s_menu_slide_x = 0;
                                s_menu_slide_x_trg = 0;
                                s_menu_prev_select = -1;
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
        (void)storage_uptime_checkpoint_save(0);
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
        ui_draw_battery_icon(pu8g2, 2, 2);
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
        ui_draw_battery_icon(pu8g2, 2, 2);
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

void display_action_pose_calib(u8g2_t *pu8g2)
{
        uint8_t retry;

        if (pu8g2 == NULL)
                return;
        display_show_text_feedback(pu8g2, "Pose Calib", "Calibrating", 300);
        sensor_request_active(UI_SENSOR_ACTIVE_HOLD_MS);
        if (!storage_run_lis2dh12_calibration_save()) {
                display_show_text_feedback(pu8g2, "Pose Calib", "Accel Failed", 900);
                return;
        }
        for (retry = 0; retry < 30U; retry++) {
                sensor_request_active(UI_SENSOR_ACTIVE_HOLD_MS);
                delay_ms(40);
                if (sensor_pose_capture_config(&s_ui_config)) {
                        ui_config_save();
                        display_show_text_feedback(pu8g2, "Pose Calib", "Saved Both", 700);
                        s_force_full_refresh = true;
                        return;
                }
        }
        if (!sensor_pose_capture_config(&s_ui_config)) {
                display_show_text_feedback(pu8g2, "Pose Calib", "No attitude", 900);
                return;
        }
        ui_config_save();
        display_show_text_feedback(pu8g2, "Pose Calib", "Saved Both", 700);
        s_force_full_refresh = true;
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

static int ui_menu_total_count(void)
{
        const ui_menu_item_t *it = s_menu_head;
        int count = 0;

        while (it != NULL) {
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

static void ui_update_frame_target(u8g2_t *pu8g2)
{
        (void)pu8g2;
        frame_y_trg = 0;
        frame_len_trg = 0;
        list_scroll_y_trg = 0;
}

#define UI_CLOCK_DIGIT_W 19
#define UI_CLOCK_DIGIT_H 46
#define UI_CLOCK_SEG_T   4
#define UI_CLOCK_GAP     3
#define UI_CLOCK_COLON_W 4
#define UI_CLOCK_COLON_GAP 5
#define UI_CLOCK_LEFT_X  2
#define UI_CLOCK_TIME_AREA_W 96
#define UI_CLOCK_TOP_Y   12
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

static void ui_draw_centered_str_visible(u8g2_t *pu8g2, short offset_x,
                                         short y, const char *str)
{
        short x0;
        uint8_t text_w;

        if (str == NULL)
                return;
        text_w = (uint8_t)u8g2_GetStrWidth(pu8g2, str);
        x0 = (short)(offset_x + (CONFIG_SCREEN_WIDTH - text_w) / 2);
        ui_draw_str_visible(pu8g2, x0, y, str);
}

static void ui_draw_battery_icon(u8g2_t *pu8g2, short x, short y)
{
        sensor_battery_t bat = {0};
        const bool valid = sensor_get_battery(&bat);
        const uint8_t pct = valid ? bat.percent : 0U;
        const short body_w = 16;
        const short body_h = 8;
        short fill_w;

        if (x < 0 || y < 0 ||
            (short)(x + body_w + 3) > CONFIG_SCREEN_WIDTH ||
            (short)(y + body_h) > CONFIG_SCREEN_HEIGHT) {
                return;
        }

        u8g2_DrawFrame(pu8g2, (u8g2_uint_t)x, (u8g2_uint_t)y,
                       (u8g2_uint_t)body_w, (u8g2_uint_t)body_h);
        u8g2_DrawBox(pu8g2, (u8g2_uint_t)(x + body_w),
                     (u8g2_uint_t)(y + 2), 2, 4);

        fill_w = (short)(((uint16_t)pct * (body_w - 2) + 99U) / 100U);
        if (fill_w > 0) {
                ui_draw_box_clipped(pu8g2, (short)(x + 1), (short)(y + 1),
                                    fill_w, (short)(body_h - 2));
        }
        if (!valid) {
                ui_draw_box_clipped(pu8g2, (short)(x + 3), (short)(y + 3), 2, 2);
                ui_draw_box_clipped(pu8g2, (short)(x + 7), (short)(y + 3), 2, 2);
                ui_draw_box_clipped(pu8g2, (short)(x + 11), (short)(y + 3), 2, 2);
        }
}

static void ui_draw_clock_battery_icon(u8g2_t *pu8g2, short offset_x)
{
        ui_draw_battery_icon(pu8g2, (short)(offset_x + 2), 2);
}

static void ui_draw_frame_clipped(u8g2_t *pu8g2, short x, short y, short w, short h)
{
        if (w <= 0 || h <= 0)
                return;
        ui_draw_box_clipped(pu8g2, x, y, w, 1);
        ui_draw_box_clipped(pu8g2, x, (short)(y + h - 1), w, 1);
        ui_draw_box_clipped(pu8g2, x, y, 1, h);
        ui_draw_box_clipped(pu8g2, (short)(x + w - 1), y, 1, h);
}

static void ui_draw_icon_clock(u8g2_t *pu8g2, short cx, short cy)
{
        ui_draw_frame_clipped(pu8g2, (short)(cx - 12), (short)(cy - 12), 24, 24);
        ui_draw_box_clipped(pu8g2, cx, (short)(cy - 8), 1, 8);
        ui_draw_box_clipped(pu8g2, cx, cy, 7, 1);
}

static void ui_draw_icon_time_set(u8g2_t *pu8g2, short cx, short cy)
{
        ui_draw_icon_clock(pu8g2, cx, cy);
        ui_draw_box_clipped(pu8g2, (short)(cx + 14), (short)(cy + 7), 9, 2);
        ui_draw_box_clipped(pu8g2, (short)(cx + 17), (short)(cy + 4), 2, 8);
}

static void ui_draw_icon_rtc_calib(u8g2_t *pu8g2, short cx, short cy)
{
        ui_draw_box_clipped(pu8g2, (short)(cx - 15), (short)(cy - 11), 30, 1);
        ui_draw_box_clipped(pu8g2, (short)(cx - 15), (short)(cy + 11), 30, 1);
        ui_draw_box_clipped(pu8g2, (short)(cx - 10), (short)(cy - 7), 2, 15);
        ui_draw_box_clipped(pu8g2, (short)(cx + 8), (short)(cy - 7), 2, 15);
        ui_draw_box_clipped(pu8g2, (short)(cx - 14), cy, 10, 2);
        ui_draw_box_clipped(pu8g2, (short)(cx + 4), cy, 10, 2);
        ui_draw_box_clipped(pu8g2, (short)(cx + 8), (short)(cy - 4), 2, 10);
}

static void ui_draw_icon_uptime_reset(u8g2_t *pu8g2, short cx, short cy)
{
        ui_draw_frame_clipped(pu8g2, (short)(cx - 12), (short)(cy - 12), 24, 24);
        ui_draw_box_clipped(pu8g2, cx, (short)(cy - 7), 1, 8);
        ui_draw_box_clipped(pu8g2, cx, cy, 6, 1);
        ui_draw_box_clipped(pu8g2, (short)(cx + 8), (short)(cy - 14), 8, 2);
        ui_draw_box_clipped(pu8g2, (short)(cx + 14), (short)(cy - 12), 2, 5);
}

static void ui_draw_icon_boot_count(u8g2_t *pu8g2, short cx, short cy)
{
        ui_draw_frame_clipped(pu8g2, (short)(cx - 13), (short)(cy - 11), 26, 22);
        ui_draw_frame_clipped(pu8g2, (short)(cx - 10), (short)(cy - 8), 20, 16);
        ui_draw_box_clipped(pu8g2, (short)(cx - 6), (short)(cy - 3), 4, 8);
        ui_draw_box_clipped(pu8g2, (short)(cx - 1), (short)(cy - 6), 4, 11);
        ui_draw_box_clipped(pu8g2, (short)(cx + 4), (short)(cy - 1), 4, 6);
}

static void ui_draw_icon_pose_calib(u8g2_t *pu8g2, short cx, short cy)
{
        ui_draw_frame_clipped(pu8g2, (short)(cx - 14), (short)(cy - 8), 28, 16);
        ui_draw_box_clipped(pu8g2, (short)(cx - 10), (short)(cy + 4), 20, 2);
        ui_draw_box_clipped(pu8g2, (short)(cx - 2), (short)(cy - 3), 5, 5);
        ui_draw_box_clipped(pu8g2, cx, (short)(cy - 14), 1, 7);
        u8g2_DrawLine(pu8g2, (u8g2_uint_t)(cx - 13), (u8g2_uint_t)(cy - 12),
                      (u8g2_uint_t)(cx - 7), (u8g2_uint_t)(cy - 12));
        u8g2_DrawLine(pu8g2, (u8g2_uint_t)(cx + 7), (u8g2_uint_t)(cy - 12),
                      (u8g2_uint_t)(cx + 13), (u8g2_uint_t)(cy - 12));
}

static void ui_draw_icon_battery_chart(u8g2_t *pu8g2, short cx, short cy)
{
        if (cx < 16 || cx > (short)(CONFIG_SCREEN_WIDTH - 16))
                return;
        ui_draw_frame_clipped(pu8g2, (short)(cx - 16), (short)(cy - 12), 32, 24);
        ui_draw_box_clipped(pu8g2, (short)(cx - 14), (short)(cy + 8), 28, 1);
        ui_draw_box_clipped(pu8g2, (short)(cx - 14), (short)(cy - 9), 1, 18);
        u8g2_DrawLine(pu8g2, (u8g2_uint_t)(cx - 12), (u8g2_uint_t)(cy + 5),
                      (u8g2_uint_t)(cx - 5), (u8g2_uint_t)(cy + 1));
        u8g2_DrawLine(pu8g2, (u8g2_uint_t)(cx - 5), (u8g2_uint_t)(cy + 1),
                      (u8g2_uint_t)(cx + 2), (u8g2_uint_t)(cy - 4));
        u8g2_DrawLine(pu8g2, (u8g2_uint_t)(cx + 2), (u8g2_uint_t)(cy - 4),
                      (u8g2_uint_t)(cx + 12), (u8g2_uint_t)(cy - 7));
}

static void ui_draw_menu_icon(u8g2_t *pu8g2, const char *name, short cx, short cy)
{
        if (name == NULL)
                return;
        if (strcmp(name, "Clock Mode") == 0)
                ui_draw_icon_clock(pu8g2, cx, cy);
        else if (strcmp(name, "Time Set") == 0)
                ui_draw_icon_time_set(pu8g2, cx, cy);
        else if (strcmp(name, "RTC Calib") == 0)
                ui_draw_icon_rtc_calib(pu8g2, cx, cy);
        else if (strcmp(name, "Uptime Reset") == 0)
                ui_draw_icon_uptime_reset(pu8g2, cx, cy);
        else if (strcmp(name, "Boot Count") == 0)
                ui_draw_icon_boot_count(pu8g2, cx, cy);
        else if (strcmp(name, "Pose Calib") == 0)
                ui_draw_icon_pose_calib(pu8g2, cx, cy);
        else if (strcmp(name, "Battery Chart") == 0)
                ui_draw_icon_battery_chart(pu8g2, cx, cy);
        else
                ui_draw_frame_clipped(pu8g2, (short)(cx - 12),
                                      (short)(cy - 12), 24, 24);
}

static void ui_draw_menu_card(u8g2_t *pu8g2, const ui_menu_item_t *item, short offset_x,
                              int page_index, int page_count)
{
        const char *name;
        char page[8];
        uint8_t page_w;

        if (item == NULL || offset_x <= (short)-CONFIG_SCREEN_WIDTH ||
            offset_x >= CONFIG_SCREEN_WIDTH) {
                return;
        }

        name = item->str;
        ui_draw_battery_icon(pu8g2, (short)(offset_x + 2), 2);
        ui_draw_menu_icon(pu8g2, name, (short)(offset_x + CONFIG_SCREEN_WIDTH / 2), 31);
        ui_draw_centered_str_visible(pu8g2, offset_x, 58, name);

        (void)snprintf(page, sizeof(page), "%d/%d", page_index + 1, page_count);
        page_w = (uint8_t)u8g2_GetStrWidth(pu8g2, page);
        ui_draw_str_visible(pu8g2, (short)(offset_x + CONFIG_SCREEN_WIDTH - page_w - 2),
                            10, page);
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

static void ui_7seg_visual_bounds(uint8_t digit, short *left, short *right)
{
        const short w = UI_CLOCK_DIGIT_W;
        const short t = UI_CLOCK_SEG_T;
        const uint8_t mask = ui_7seg_mask(digit);
        short l = w;
        short r = 0;

        if ((mask & (UI_CLOCK_SEG_A | UI_CLOCK_SEG_D | UI_CLOCK_SEG_G)) != 0u) {
                if (l > t)
                        l = t;
                if (r < (short)(w - t))
                        r = (short)(w - t);
        }
        if ((mask & (UI_CLOCK_SEG_E | UI_CLOCK_SEG_F)) != 0u) {
                l = 0;
                if (r < t)
                        r = t;
        }
        if ((mask & (UI_CLOCK_SEG_B | UI_CLOCK_SEG_C)) != 0u) {
                if (l > (short)(w - t))
                        l = (short)(w - t);
                r = w;
        }
        if (l > r) {
                l = 0;
                r = w;
        }
        if (left != NULL)
                *left = l;
        if (right != NULL)
                *right = r;
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
        const uint8_t digits[4] = {
                (uint8_t)(hour / 10u),
                (uint8_t)(hour % 10u),
                (uint8_t)(minute / 10u),
                (uint8_t)(minute % 10u),
        };
        short left[4];
        short right[4];
        short rel_x[4];
        short colon_x;
        short total_w;
        short base_x;

        for (uint8_t i = 0; i < 4u; i++)
                ui_7seg_visual_bounds(digits[i], &left[i], &right[i]);

        rel_x[0] = (short)-left[0];
        rel_x[1] = (short)(rel_x[0] + right[0] + UI_CLOCK_GAP - left[1]);
        colon_x = (short)(rel_x[1] + right[1] + UI_CLOCK_COLON_GAP);
        rel_x[2] = (short)(colon_x + UI_CLOCK_COLON_W + UI_CLOCK_COLON_GAP -
                           left[2]);
        rel_x[3] = (short)(rel_x[2] + right[2] + UI_CLOCK_GAP - left[3]);
        total_w = (short)(rel_x[3] + right[3]);
        base_x = (short)(x + (UI_CLOCK_TIME_AREA_W - total_w) / 2);

        ui_draw_7seg_digit(pu8g2, (short)(base_x + rel_x[0]), y, digits[0]);
        ui_draw_7seg_digit(pu8g2, (short)(base_x + rel_x[1]), y, digits[1]);
        ui_draw_7seg_colon(pu8g2, (short)(base_x + colon_x), y);
        ui_draw_7seg_digit(pu8g2, (short)(base_x + rel_x[2]), y, digits[2]);
        ui_draw_7seg_digit(pu8g2, (short)(base_x + rel_x[3]), y, digits[3]);
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

        ui_draw_clock_battery_icon(pu8g2, offset_x);
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

#define UI_BAT_CHART_X 7
#define UI_BAT_CHART_Y 17
#define UI_BAT_CHART_W 116
#define UI_BAT_CHART_H 37
#define UI_BAT_CHART_V_MIN_MV 3000U
#define UI_BAT_CHART_V_MAX_MV 4200U

static short ui_chart_map_x(const storage_battery_history_point_t *p,
                            uint32_t t_min, uint32_t t_max)
{
        const uint32_t range = (t_max > t_min) ? (t_max - t_min) : 1U;
        const uint32_t rel = (p->uptime_s > t_min) ? (p->uptime_s - t_min) : 0U;

        return (short)(UI_BAT_CHART_X +
                       ((rel * (UI_BAT_CHART_W - 1U) + range / 2U) / range));
}

static short ui_chart_map_y(uint32_t value, uint32_t y_min, uint32_t y_max)
{
        const uint32_t range = (y_max > y_min) ? (y_max - y_min) : 1U;

        if (value < y_min)
                value = y_min;
        if (value > y_max)
                value = y_max;
        return (short)(UI_BAT_CHART_Y + UI_BAT_CHART_H - 1 -
                       (((value - y_min) * (UI_BAT_CHART_H - 1U) +
                         range / 2U) / range));
}

static void ui_chart_format_span(char *buf, size_t len, uint32_t span_s)
{
        if (buf == NULL || len == 0U)
                return;
        if (span_s < 3600U) {
                const uint32_t minutes = (span_s + 30U) / 60U;

                (void)snprintf(buf, len, "%lum", (unsigned long)minutes);
        } else if (span_s < 36000U) {
                const uint32_t tenths = (span_s * 10U + 1800U) / 3600U;

                (void)snprintf(buf, len, "%lu.%luh",
                               (unsigned long)(tenths / 10U),
                               (unsigned long)(tenths % 10U));
        } else {
                const uint32_t hours = (span_s + 1800U) / 3600U;

                (void)snprintf(buf, len, "%luh", (unsigned long)hours);
        }
}

static void ui_chart_range(const storage_battery_history_point_t *points,
                           uint16_t count, bool percent_mode,
                           uint32_t *out_min, uint32_t *out_max)
{
        uint32_t min_v = percent_mode ? 100U : UI_BAT_CHART_V_MAX_MV;
        uint32_t max_v = percent_mode ? 0U : UI_BAT_CHART_V_MIN_MV;
        uint32_t pad;

        for (uint16_t i = 0; i < count; i++) {
                const uint32_t value = percent_mode ? points[i].percent :
                                       points[i].millivolts;

                if (value < min_v)
                        min_v = value;
                if (value > max_v)
                        max_v = value;
        }

        pad = percent_mode ? 2U : 20U;
        if (max_v <= min_v)
                max_v = min_v + pad;
        if (max_v - min_v < pad * 2U) {
                const uint32_t mid = (min_v + max_v) / 2U;

                min_v = (mid > pad) ? (mid - pad) : 0U;
                max_v = mid + pad;
        } else {
                min_v = (min_v > pad) ? (min_v - pad) : 0U;
                max_v += pad;
        }

        if (percent_mode) {
                if (max_v > 100U)
                        max_v = 100U;
        } else {
                if (min_v < UI_BAT_CHART_V_MIN_MV)
                        min_v = UI_BAT_CHART_V_MIN_MV;
                if (max_v > UI_BAT_CHART_V_MAX_MV)
                        max_v = UI_BAT_CHART_V_MAX_MV;
                if (max_v <= min_v)
                        max_v = min_v + 1U;
        }
        *out_min = min_v;
        *out_max = max_v;
}

static void ui_draw_battery_chart(u8g2_t *pu8g2,
                                  const storage_battery_history_point_t *points,
                                  uint16_t count, bool percent_mode)
{
        sensor_battery_t bat = {0};
        const bool has_bat = sensor_get_battery(&bat);
        char line[24];

        u8g2_ClearBuffer(pu8g2);
        ui_draw_battery_icon(pu8g2, 2, 2);
        if (has_bat) {
                if (percent_mode)
                        (void)snprintf(line, sizeof(line), "BAT %u%%",
                                       (unsigned)bat.percent);
                else
                        (void)snprintf(line, sizeof(line), "BAT %u.%02uV %u%%",
                                       (unsigned)(bat.millivolts / 1000U),
                                       (unsigned)((bat.millivolts % 1000U) / 10U),
                                       (unsigned)bat.percent);
        } else {
                (void)snprintf(line, sizeof(line), "BAT --");
        }
        ui_draw_str_visible(pu8g2, 24, 10, line);

        ui_draw_frame_clipped(pu8g2, UI_BAT_CHART_X, UI_BAT_CHART_Y,
                              UI_BAT_CHART_W, UI_BAT_CHART_H);
        for (uint8_t i = 1; i < 3u; i++) {
                const short y = (short)(UI_BAT_CHART_Y +
                                        (UI_BAT_CHART_H * i) / 3);

                for (short x = (short)(UI_BAT_CHART_X + 2);
                     x < (short)(UI_BAT_CHART_X + UI_BAT_CHART_W - 1);
                     x = (short)(x + 4)) {
                        ui_draw_box_clipped(pu8g2, x, y, 1, 1);
                }
        }

        if (!storage_system_ready()) {
                ui_draw_centered_str_visible(pu8g2, 0, 40, "FS WAIT");
                u8g2_SendBuffer(pu8g2);
                return;
        }
        if (count == 0U) {
                ui_draw_centered_str_visible(pu8g2, 0, 40, "No history");
                u8g2_SendBuffer(pu8g2);
                return;
        }

        {
                const uint32_t t_min = points[0].uptime_s;
                const uint32_t t_max = points[count - 1U].uptime_s;
                uint32_t y_min = 0U;
                uint32_t y_max = 0U;
                short prev_x = 0;
                short prev_y = 0;

                ui_chart_range(points, count, percent_mode, &y_min, &y_max);
                for (uint16_t i = 0; i < count; i++) {
                        const uint32_t value = percent_mode ?
                                               points[i].percent :
                                               points[i].millivolts;
                        const short px = ui_chart_map_x(&points[i], t_min, t_max);
                        const short py = ui_chart_map_y(value, y_min, y_max);

                        if (i == 0U) {
                                ui_draw_box_clipped(pu8g2, px, py, 2, 2);
                        } else {
                                u8g2_DrawLine(pu8g2, (u8g2_uint_t)prev_x,
                                              (u8g2_uint_t)prev_y,
                                              (u8g2_uint_t)px,
                                              (u8g2_uint_t)py);
                        }
                        prev_x = px;
                        prev_y = py;
                }
                ui_draw_box_clipped(pu8g2, (short)(prev_x - 1), (short)(prev_y - 1), 3, 3);

                {
                        char span[8];

                        ui_chart_format_span(span, sizeof(span),
                                             (t_max > t_min) ? (t_max - t_min) : 0U);
                        if (percent_mode) {
                                (void)snprintf(line, sizeof(line), "%lu-%lu%% %s",
                                               (unsigned long)y_min,
                                               (unsigned long)y_max, span);
                        } else {
                                (void)snprintf(line, sizeof(line), "%lu.%02lu-%lu.%02luV %s",
                                               (unsigned long)(y_min / 1000U),
                                               (unsigned long)((y_min % 1000U) / 10U),
                                               (unsigned long)(y_max / 1000U),
                                               (unsigned long)((y_max % 1000U) / 10U),
                                               span);
                        }
                        ui_draw_str_visible(pu8g2, 8, 64, line);
                }
        }
        u8g2_SendBuffer(pu8g2);
}

void display_action_battery_chart(u8g2_t *pu8g2)
{
        static storage_battery_history_point_t points[STORAGE_BATTERY_HISTORY_MAX_POINTS];
        bool percent_mode = false;

        if (pu8g2 == NULL)
                return;

        for (;;) {
                gesture_key_event_t evt = GESTURE_KEY_NONE;
                uint16_t count;
                sensor_battery_t bat = {0};

                if (sensor_get_battery(&bat))
                        (void)storage_battery_history_record(bat.millivolts, bat.percent);

                count = storage_battery_history_get(points,
                                                    STORAGE_BATTERY_HISTORY_MAX_POINTS);
                ui_draw_battery_chart(pu8g2, points, count, percent_mode);

                while (gesture_key_event_get(&evt)) {
                        if (evt == GESTURE_KEY_SINGLE_CLICK) {
                                percent_mode = !percent_mode;
                        } else if (evt == GESTURE_KEY_DOUBLE_CLICK) {
                                s_force_full_refresh = true;
                                return;
                        }
                }
                delay_ms(500);
        }
}

static void ui_begin_menu_slide(signed char old_select, signed char new_select,
                                int8_t visual_delta)
{
        if (old_select == new_select)
                return;
        s_menu_prev_select = old_select;
        s_menu_slide_dir = (visual_delta >= 0) ? 1 : -1;
        s_menu_slide_x = (short)(s_menu_slide_dir * CONFIG_SCREEN_WIDTH);
        s_menu_slide_x_trg = 0;
}

static bool ui_select_menu_delta_cyclic(u8g2_t *pu8g2, int8_t delta)
{
        const int list_len = ui_menu_count();
        const signed char old_select = ui_select;

        if (list_len <= 1 || delta == 0 || s_menu_slide_x != 0)
                return false;

        if (delta > 0) {
                ui_select = (signed char)((ui_select + 1) % list_len);
        } else {
                ui_select = (ui_select <= 0) ? (signed char)(list_len - 1) :
                            (signed char)(ui_select - 1);
        }
        ui_flag = (ui_select == 0);

        ui_begin_menu_slide(old_select, ui_select, delta);
        ui_update_frame_target(pu8g2);
        s_ui_config.ui_select = ui_select;
        ui_config_mark_dirty();
        s_force_full_refresh = true;
        return true;
}

static bool ui_handle_menu_tilt(u8g2_t *pu8g2)
{
        int8_t delta = 0;
        const uint32_t now_ms = HAL_GetTick();
        sensor_tilt_event_t event = SENSOR_TILT_EVENT_NONE;

        if (s_ui_mode != UI_MODE_MENU || s_rotation_anim_active)
                return false;
        if (!sensor_tilt_event_get(&event))
                return false;
        if (event == SENSOR_TILT_EVENT_RIGHT)
                delta = 1;
        else if (event == SENSOR_TILT_EVENT_LEFT)
                delta = -1;
        else {
                s_last_menu_tilt_dir = SENSOR_6D_DIR_UNKNOWN;
                return false;
        }

        if ((sensor_6d_dir_t)event == s_last_menu_tilt_dir &&
            (uint32_t)(now_ms - s_last_menu_tilt_ms) < UI_TILT_MENU_COOLDOWN_MS) {
                return false;
        }
        if (!ui_select_menu_delta_cyclic(pu8g2, delta))
                return false;

        DBG_PRINTF("[ui_tilt] event=%d delta=%d sel=%d\n",
                   (int)event, (int)delta, (int)ui_select);
        s_last_menu_tilt_ms = now_ms;
        s_last_menu_tilt_dir = (sensor_6d_dir_t)event;
        return true;
}

static const u8g2_cb_t *ui_rotation_from_dir(sensor_6d_dir_t dir)
{
        if (dir == SENSOR_6D_DIR_BACKWARD)
                return U8G2_R2;
        if (dir == SENSOR_6D_DIR_FORWARD)
                return U8G2_R0;
        return NULL;
}

static void ui_begin_rotation_transition(const u8g2_cb_t *rot)
{
        if (rot == NULL)
                return;
        if (rot == s_display_rot) {
                if (s_rotation_anim_active && s_rotation_anim_out) {
                        s_rotation_pending_rot = NULL;
                        s_rotation_anim_x_trg = 0;
                        s_force_full_refresh = true;
                }
                return;
        }
        s_rotation_pending_rot = rot;
        if (s_rotation_anim_active)
                return;

        s_rotation_anim_active = true;
        s_rotation_anim_out = true;
        s_rotation_anim_x = 0;
        s_rotation_anim_x_trg = CONFIG_SCREEN_WIDTH;
        s_force_full_refresh = true;
}

static bool ui_service_rotation_transition(u8g2_t *pu8g2)
{
        bool active = false;

        if (!s_rotation_anim_active)
                return false;

        active = true;
        if (s_rotation_anim_out && s_rotation_anim_x == CONFIG_SCREEN_WIDTH) {
                if (s_rotation_pending_rot != NULL) {
                        s_display_rot = s_rotation_pending_rot;
                        s_rotation_pending_rot = NULL;
                        u8g2_SetDisplayRotation(pu8g2, s_display_rot);
                }
                s_rotation_anim_out = false;
                s_rotation_anim_x = (short)-CONFIG_SCREEN_WIDTH;
                s_rotation_anim_x_trg = 0;
                s_force_full_refresh = true;
        } else if (s_rotation_anim_out && s_rotation_pending_rot == NULL &&
                   s_rotation_anim_x == 0) {
                s_rotation_anim_active = false;
                active = false;
        } else if (!s_rotation_anim_out && s_rotation_anim_x == 0) {
                s_rotation_anim_active = false;
                active = false;
        }
        return active;
}

static void ui_draw_menu(u8g2_t *pu8g2, short offset_x)
{
        const int list_len = ui_menu_count();
        const ui_menu_item_t *item = ui_menu_get_by_index(ui_select);

        if (s_menu_prev_select >= 0 && s_menu_slide_x != 0) {
                const ui_menu_item_t *prev = ui_menu_get_by_index(s_menu_prev_select);

                ui_draw_menu_card(pu8g2, prev,
                                  (short)(offset_x + s_menu_slide_x -
                                          s_menu_slide_dir * CONFIG_SCREEN_WIDTH),
                                  (int)s_menu_prev_select, list_len);
        }
        ui_draw_menu_card(pu8g2, item, (short)(offset_x + s_menu_slide_x),
                          (int)ui_select, list_len);
}

static bool ui_update_animation(void)
{
        const bool anim_view = ui_run(&s_view_x, &s_view_x_trg, 12, 3) != 0;
        const bool anim_y = ui_run(&frame_y, &frame_y_trg, 5, 4) != 0;
        const bool anim_len = ui_run(&frame_len, &frame_len_trg, 10, 5) != 0;
        const bool anim_scroll = ui_run(&list_scroll_y, &list_scroll_y_trg, 5, 4) != 0;
        const bool anim_menu_slide = ui_run(&s_menu_slide_x, &s_menu_slide_x_trg, 16, 4) != 0;
        const bool anim_rotation = ui_run(&s_rotation_anim_x, &s_rotation_anim_x_trg, 16, 4) != 0;

        if (!anim_menu_slide && s_menu_slide_x == 0)
                s_menu_prev_select = -1;
        return anim_view || anim_y || anim_len || anim_scroll || anim_menu_slide || anim_rotation;
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
        short rotation_x;

        animating = ui_update_animation();
        if (ui_service_rotation_transition(pu8g2))
                animating = true;
        if (!force_full && !animating && !clock_due)
                return false;
        rotation_x = s_rotation_anim_active ? s_rotation_anim_x : 0;
        u8g2_ClearBuffer(pu8g2);
        ui_draw_clock(pu8g2, (short)(-s_view_x + rotation_x));
        ui_draw_menu(pu8g2, (short)(CONFIG_SCREEN_WIDTH - s_view_x + rotation_x));
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
                        s_menu_slide_x = 0;
                        s_menu_slide_x_trg = 0;
                        s_menu_prev_select = -1;
                } else {
                        ui_config_deferred_save(true);
                        s_ui_mode = UI_MODE_CLOCK;
                        s_view_x_trg = 0;
                        s_menu_slide_x = 0;
                        s_menu_slide_x_trg = 0;
                        s_menu_prev_select = -1;
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
                (void)ui_select_menu_delta_cyclic(pu8g2, 1);
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
        const sensor_6d_dir_t d = sensor_get_6d_dir();
        const u8g2_cb_t *rot = ui_rotation_from_dir(d);
        bool active = false;

        if (rot != NULL && rot != s_display_rot) {
                ui_begin_rotation_transition(rot);
                active = true;
        }
        if (rtc_clock_poll()) {
                s_force_full_refresh = true;
                active = true;
        }
        if (s_ui_mode == UI_MODE_MENU) {
                sensor_request_active(UI_SENSOR_ACTIVE_HOLD_MS);
                active = true;
        }
        key_scan();
        if (!key_msg.update_flag && ui_handle_menu_tilt(pu8g2))
                active = true;
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
        DBG_PRINTF("[ui_task] ui_test begin\n");
        u8g2Init(pu8g2);
        DBG_PRINTF("[ui_task] ui_test oled init done\n");
        /* Use larger imported font from local u8g2 source. */
        u8g2_SetFont(pu8g2, u8g2_font_6x12_tf);
        s_display_rot = U8G2_R2;
        u8g2_SetDisplayRotation(pu8g2, s_display_rot);
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
        DBG_PRINTF("[ui_task] ui_test splash shown\n");
        dly_ms(UI_BOOT_IMAGE_MS);
        DBG_PRINTF("[ui_task] ui_test delay done\n");
        frame_y = frame_y_trg = 0;
        ui_update_frame_target(pu8g2);
        frame_len = frame_len_trg;
        list_scroll_y = list_scroll_y_trg;
        s_ui_mode = UI_MODE_CLOCK;
        s_view_x = 0;
        s_view_x_trg = 0;
        s_menu_slide_x = 0;
        s_menu_slide_x_trg = 0;
        s_menu_prev_select = -1;
        s_last_menu_tilt_ms = 0;
        s_last_menu_tilt_dir = SENSOR_6D_DIR_UNKNOWN;
        s_rotation_anim_x = 0;
        s_rotation_anim_x_trg = 0;
        s_rotation_anim_active = false;
        s_rotation_anim_out = false;
        s_rotation_pending_rot = NULL;
        s_last_clock_raw_seconds = UI_CLOCK_RAW_UNSET;
        DBG_PRINTF("[ui_task] ui_test end\n");
#endif
}

void ui_task(void *arg)
{
        int list_len;
        int8_t init_sel = 0;
        (void)arg;

        DBG_PRINTF("[ui_task] start\n");
        ui_config_load();
        init_sel = s_ui_config.ui_select;

        if (!s_menu_defaults_registered) {
                ui_menu_registry_register_all(ui_menu_register);
                s_menu_defaults_registered = true;
        }
        list_len = ui_menu_count();
        DBG_PRINTF("[ui_task] menu total=%d visible=%d mode=%u\n",
                   ui_menu_total_count(), list_len,
                   (unsigned int)s_ui_config.clock_display_mode);
        if (list_len <= 0) {
                DBG_PRINTF("[ui_task] menu empty, registering fallback\n");
                (void)ui_menu_register("Clock Mode", display_action_clock_mode,
                                       UI_MENU_MODE_ALL);
                s_ui_config.clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
                ui_config_sanitize(&s_ui_config);
                list_len = ui_menu_count();
        }
        if (list_len <= 0)
                list_len = 1;
        if (init_sel < 0)
                init_sel = 0;
        if (init_sel >= list_len)
                init_sel = list_len - 1;
        DBG_PRINTF("[ui_task] menu_count=%d init_sel=%d\n", list_len, (int)init_sel);
        ui_select = init_sel;
        ui_flag = (init_sel == 0);
        ui_test(&u8g2);
        DBG_PRINTF("[ui_task] boot splash done storage_ready=%d\n",
                   storage_system_ready());
#if OLED_RAW_TEST_MODE
        for (;;) {
                delay_ms(1000);
        }
#else
        {
                const uint32_t storage_wait_start_ms = HAL_GetTick();

                while (!storage_system_ready() &&
                       (uint32_t)(HAL_GetTick() - storage_wait_start_ms) <
                       UI_STORAGE_BOOT_WAIT_MS) {
                        oled_boot_splash_show(&u8g2);
                        delay_ms(UI_STORAGE_BOOT_POLL_MS);
                }
                if (!storage_system_ready())
                        display_show_text_feedback(&u8g2, "Storage", "FS WAIT", 700);
                DBG_PRINTF("[ui_task] storage wait done ready=%d elapsed=%lu\n",
                           storage_system_ready(),
                           (unsigned long)(HAL_GetTick() - storage_wait_start_ms));
        }
        display_reload_persistent_config();
        ui_update_frame_target(&u8g2);
        s_last_uptime_save_ms = ui_tick_ms();
        DBG_PRINTF("[ui_task] entering main loop\n");
        for (;;) {
                if (s_shutdown_prompt_show) {
                        const uint8_t pct = s_shutdown_prompt_progress_pct;
                        const uint8_t bar_w = (uint8_t)((108u * pct) / 100u);

                        ui_config_deferred_save(true);
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
                ui_periodic_uptime_save();
                ui_config_deferred_save(false);
                const bool active = loop1(&u8g2);
                /* Lower refresh load while keeping responsive interaction. */
                {
                        const uint32_t delay_ms_value = active ? 10U : 100U;

                        delay_ms(delay_ms_value);
                }
        }
#endif
}
