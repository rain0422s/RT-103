/**
 * @file display.c
 * U8G2 OLED UI
 */
#include "display.h"
#include "storage.h"
#include "sensor.h"
#include "gesture.h"
#include "lfs_port.h"
#include <stdio.h>

extern const uint8_t u8g2_font_6x12_tf[];

static u8g2_t u8g2;

typedef struct {
        uint8_t id;
        uint8_t press;
        uint8_t long_press;
        uint8_t update_flag;
} key_msg_t;

typedef struct {
        const char *str;
        uint8_t len;
} setting_item_t;

#define UI_INDEX_LIST   0
#define UI_INDEX_CALIB  8
static const setting_item_t list[] = {
        {"list", 4},
        {"ab", 2},
        {"abc", 3},
        {"abcd", 4},
        {"temp", 4},
        {"humi", 4},
        {"press", 5},
        {"light", 5},
        {"cal", 3},   /* 选中此项并按键：执行 LIS2DH12 校准并保存到 EEPROM */
};

static short frame_len, frame_len_trg;
static short frame_y, frame_y_trg;
static short list_scroll_y, list_scroll_y_trg;
static short x = 0;
static signed char ui_select = 0;
static bool ui_flag = true;
static short s_line_h = 18;
static short s_frame_h = 20;
static short s_text_x = 2;
static short s_text_y0 = 13;
#define UI_BOOT_IMAGE_MS 700

static key_msg_t key_msg = {0};
static bool s_force_full_refresh = true;
static char s_boot_item_text[24] = "boot:--";

static short abs_s(short i)
{
        return (i < 0) ? (short)(~(i - 1)) : i;
}

static void ui_refresh_boot_item_text(void)
{
        static uint32_t s_last_boot_count = 0xFFFFFFFFu;
        static int s_last_mounted = -1;
        const int mounted = (lfs_get() != NULL) ? 1 : 0;

        if (!mounted) {
                if (s_last_mounted != 0) {
                        s_last_mounted = 0;
                        (void)snprintf(s_boot_item_text, sizeof(s_boot_item_text), "boot:--");
                        s_force_full_refresh = true;
                }
                return;
        }

        {
                const uint32_t boot_count = lfs_get_boot_count();
                if (s_last_mounted != 1 || boot_count != s_last_boot_count) {
                        s_last_mounted = 1;
                        s_last_boot_count = boot_count;
                        (void)snprintf(s_boot_item_text, sizeof(s_boot_item_text), "boot:%lu", (unsigned long)boot_count);
                        s_force_full_refresh = true;
                }
        }
}

static void key_scan(void)
{
        gesture_key_event_t evt = GESTURE_KEY_NONE;
        while (gesture_key_event_get(&evt)) {
                if (evt == GESTURE_KEY_SINGLE_CLICK || evt == GESTURE_KEY_DOUBLE_CLICK || evt == GESTURE_KEY_LONG_PRESS) {
                        key_msg.id = 0;
                        key_msg.press = (evt != GESTURE_KEY_LONG_PRESS);
                        key_msg.long_press = (evt == GESTURE_KEY_LONG_PRESS);
                        key_msg.update_flag = 1;
                }
        }
}

static uint8_t ui_run(short *a, short *a_trg, uint8_t step, uint8_t slow_cnt)
{
        uint8_t temp = (abs_s(*a_trg - *a) > slow_cnt) ? step : 1;
        if (*a < *a_trg)
                *a += temp;
        else if (*a > *a_trg)
                *a -= temp;
        else
                return 0;
        return 1;
}

static void ui_update_frame_target(u8g2_t *pu8g2)
{
        const short list_len = (short)(sizeof(list) / sizeof(list[0]));
        short visible_rows = (short)((CONFIG_SCREEN_HEIGHT - 2) / s_line_h);
        short top_index = 0;
        const char *selected_text = (ui_select == 1) ? s_boot_item_text : list[ui_select].str;
        const uint8_t text_w = (uint8_t)u8g2_GetStrWidth(pu8g2, selected_text);

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

static bool ui_show(u8g2_t *pu8g2, bool force_full)
{
        const int list_len = sizeof(list) / sizeof(list[0]);
        const short prev_frame_y = frame_y;
        const short prev_frame_len = frame_len;

        u8g2_ClearBuffer(pu8g2);
        for (int i = 0; i < list_len; i++) {
                const short yy = (short)(s_text_y0 + i * s_line_h - list_scroll_y);
                const char *line = (i == 1) ? s_boot_item_text : list[i].str;
                u8g2_DrawStr(pu8g2, s_text_x, yy, line);
        }
        u8g2_DrawRFrame(pu8g2, x, (short)(frame_y - list_scroll_y), frame_len, s_frame_h, 3);
        {
                const bool anim_y = ui_run(&frame_y, &frame_y_trg, 5, 4) != 0;
                const bool anim_len = ui_run(&frame_len, &frame_len_trg, 10, 5) != 0;
                const bool anim_scroll = ui_run(&list_scroll_y, &list_scroll_y_trg, 5, 4) != 0;
                const bool animating = anim_y || anim_len || anim_scroll;
                (void)force_full;
                (void)prev_frame_y;
                (void)prev_frame_len;
                u8g2_SendBuffer(pu8g2);
                return animating;
        }
}

static bool ui_proc(u8g2_t *pu8g2)
{
        const int list_len = sizeof(list) / sizeof(list[0]);
        bool got_ui_event = false;
        if (key_msg.update_flag && key_msg.long_press) {
                key_msg.update_flag = 0;
                key_msg.long_press = 0;
                key_msg.press = 0;
                got_ui_event = true;
                if (ui_select == UI_INDEX_LIST) {
                        oled_boot_splash_show(pu8g2);
                        delay_ms(UI_BOOT_IMAGE_MS);
                        s_force_full_refresh = true;
                }
        } else if (key_msg.update_flag && key_msg.press) {
                key_msg.update_flag = 0;
                key_msg.press = 0;
                key_msg.long_press = 0;
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
                {
                        eeprom_config_t c = {
                                .magic = EEPROM_MAGIC,
                                .version = EEPROM_CONFIG_VERSION,
                                .ui_select = ui_select,
                                .reserved = 0,
                        };
                        storage_save_config(&c);
                }
                /* 选中「cal」时按键：执行零 g 校准并保存到 EEPROM */
                if (ui_select == UI_INDEX_CALIB)
                        sensor_lis2dh12_calibrate_and_save();
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
        ui_refresh_boot_item_text();
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
#endif
}

void ui_task(void *arg)
{
        const int list_len = sizeof(list) / sizeof(list[0]);
        int8_t init_sel = (int8_t)(intptr_t)arg;
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
        for (;;) {
                const bool active = loop1(&u8g2);
                /* Lower refresh load while keeping responsive interaction. */
                delay_ms(active ? 10 : 100);
        }
#endif
}
