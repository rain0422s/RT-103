/**
 * @file display.c
 * LVGL demo + U8G2 OLED UI (when U8G2_ENABLED)
 */
#include "display.h"

/* ========== LVGL：U8G2_ENABLED 未启用时走 LVGL ========== */
#ifndef U8G2_ENABLED
void demo_run(void)
{
        lv_init();
        lv_port_disp_init();
        while (1) {
                delay_ms(10);
                lv_timer_handler();
        }
}
#endif

/* ========== U8G2：U8G2_ENABLED 为 1 时走 U8G2，不走 LVGL ========== */
#ifdef U8G2_ENABLED
#define CHECK_KEY(n)  ((n) ? HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) : HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1))

static u8g2_t u8g2;

typedef struct {
        uint8_t val;
        uint8_t last_val;
} key_t;

typedef struct {
        uint8_t id;
        uint8_t press;
        uint8_t update_flag;
        uint8_t res;
} key_msg_t;

typedef struct {
        const char *str;
        uint8_t len;
} setting_item_t;

static const setting_item_t list[] = {
        {"list", 4},
        {"ab", 2},
        {"abc", 3},
        {"abcd", 4},
};

static short frame_len, frame_len_trg;
static short frame_y, frame_y_trg;
static short x, y = 13;
static signed char ui_select = 0;
static bool ui_flag = true;

static key_t key[2] = {0};
static key_msg_t key_msg = {0};

static short abs_s(short i)
{
        return (i < 0) ? (short)(~(i - 1)) : i;
}

static void key_scan(void)
{
        for (int i = 0; i < 2; i++) {
                key[i].val = CHECK_KEY(i);
                if (key[i].val != key[i].last_val) {
                        key[i].last_val = key[i].val;
                        if (key[i].val == 0) {
                                key_msg.id = i;
                                key_msg.press = 1;
                                key_msg.update_flag = 1;
                        }
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

static void ui_show(u8g2_t *pu8g2)
{
        const int list_len = sizeof(list) / sizeof(list[0]);
        u8g2_ClearBuffer(pu8g2);
        for (int i = 0; i < list_len; i++)
                u8g2_DrawStr(pu8g2, x + 2, y + i * 18, list[i].str);
        u8g2_DrawRFrame(pu8g2, x, frame_y, frame_len, 20, 3);
        ui_run(&frame_y, &frame_y_trg, 5, 4);
        ui_run(&frame_len, &frame_len_trg, 10, 5);
        u8g2_SendBuffer(pu8g2);
}

static void ui_proc(u8g2_t *pu8g2)
{
        const int list_len = sizeof(list) / sizeof(list[0]);
        if (key_msg.update_flag && key_msg.press) {
                key_msg.update_flag = 0;
                if (ui_flag) {
                        ui_select++;
                        if (ui_select == list_len - 1)
                                ui_flag = false;
                } else {
                        ui_select--;
                        if (ui_select == 0)
                                ui_flag = true;
                }
                frame_y_trg = ui_select * 15;
                frame_len_trg = list[ui_select].len * 13;
        }
        ui_show(pu8g2);
}

static void loop1(u8g2_t *pu8g2)
{
        key_scan();
        ui_proc(pu8g2);
}

void ui_test(u8g2_t *pu8g2)
{
        u8g2Init(pu8g2);
        delay_ms(1000);
        u8g2_DrawLine(pu8g2, 0, 0, 127, 63);
        u8g2_DrawLine(pu8g2, 127, 0, 0, 63);
        u8g2_SendBuffer(pu8g2);
        delay_ms(1000);
        u8g2_ClearBuffer(pu8g2);
        delay_ms(1000);
        u8g2_DrawXBMP(pu8g2, 30, 20, 25, 25, u8g_logo_bits);
        u8g2_SendBuffer(pu8g2);
        u8g2_SetFont(pu8g2, u8g2_font_u8glib_4_tf);
        frame_len = frame_len_trg = list[ui_select].len * 12;
}

void ui_task(void *arg)
{
        (void)arg;
        ui_test(&u8g2);
        for (;;) {
                loop1(&u8g2);
                delay_ms(500);
        }
}
#endif /* U8G2_ENABLED */
