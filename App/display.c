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

extern const uint8_t u8g2_font_6x12_tf[];

static u8g2_t u8g2;

typedef struct {
        uint8_t id;
        uint8_t press;
        uint8_t long_press;
        uint8_t update_flag;
} key_msg_t;

typedef struct ui_menu_item {
        const char *str;
        void (*on_long_press)(u8g2_t *pu8g2);
        struct ui_menu_item *next;
} ui_menu_item_t;

static bool ui_menu_register(const char *name, void (*on_long_press)(u8g2_t *pu8g2));
static int ui_menu_count(void);
static const ui_menu_item_t *ui_menu_get_by_index(int idx);

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
static short s_line_h = 18;
static short s_frame_h = 20;
static short s_text_x = 2;
static short s_text_y0 = 13;
#define UI_BOOT_IMAGE_MS 700

static key_msg_t key_msg = {0};
static bool s_force_full_refresh = true;
static volatile bool s_shutdown_prompt_show;
static volatile bool s_shutdown_prompt_confirmed;
static volatile uint8_t s_shutdown_prompt_progress_pct;

static short abs_s(short i)
{
        return (i < 0) ? (short)(~(i - 1)) : i;
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
        delay_ms(hold_ms);
}

static bool ui_menu_register(const char *name, void (*on_long_press)(u8g2_t *pu8g2))
{
        ui_menu_item_t *node;

        if (name == NULL || s_menu_node_count >= UI_MENU_MAX_ITEMS)
                return false;
        node = &s_menu_nodes[s_menu_node_count];
        node->str = name;
        node->on_long_press = on_long_press;
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
        return ui_menu_register(name, on_long_press);
}

static int ui_menu_count(void)
{
        return (int)s_menu_node_count;
}

static const ui_menu_item_t *ui_menu_get_by_index(int idx)
{
        const ui_menu_item_t *it = s_menu_head;
        int i = 0;

        if (idx < 0)
                return NULL;
        while (it != NULL && i < idx) {
                it = it->next;
                i++;
        }
        return (i == idx) ? it : NULL;
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

/** Vertical scroll indicator: opposite side of list text (right edge). */
static void ui_draw_list_scroll_indicator(u8g2_t *pu8g2)
{
        const int list_len = ui_menu_count();
        short visible_rows = (short)((CONFIG_SCREEN_HEIGHT - 2) / s_line_h);
        const short track_x = (short)(CONFIG_SCREEN_WIDTH - 5);
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

static bool ui_show(u8g2_t *pu8g2, bool force_full)
{
        const int list_len = ui_menu_count();
        const short prev_frame_y = frame_y;
        const short prev_frame_len = frame_len;
        const ui_menu_item_t *it = s_menu_head;
        int i = 0;

        u8g2_ClearBuffer(pu8g2);
        while (it != NULL && i < list_len) {
                const short yy = (short)(s_text_y0 + i * s_line_h - list_scroll_y);
                const char *line = it->str;
                u8g2_DrawStr(pu8g2, s_text_x, yy, line);
                it = it->next;
                i++;
        }
        u8g2_DrawRFrame(pu8g2, x, (short)(frame_y - list_scroll_y), frame_len, s_frame_h, 3);
        ui_draw_list_scroll_indicator(pu8g2);
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
        const int list_len = ui_menu_count();
        bool got_ui_event = false;
        if (list_len <= 0)
                return ui_show(pu8g2, true);
        if (key_msg.update_flag && key_msg.long_press) {
                key_msg.update_flag = 0;
                key_msg.long_press = 0;
                key_msg.press = 0;
                got_ui_event = true;
                ui_execute_selected_action(pu8g2);
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
#endif
}

void ui_task(void *arg)
{
        int list_len;
        int8_t init_sel = (int8_t)(intptr_t)arg;

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
                /* Lower refresh load while keeping responsive interaction. */
                delay_ms(active ? 10 : 100);
        }
#endif
}
