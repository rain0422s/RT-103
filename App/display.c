/**
 * @file display.c
 * U8G2 OLED UI
 */
#include "display.h"
#include "storage.h"
#include "sensor.h"

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

#define UI_INDEX_CALIB  4
static const setting_item_t list[] = {
        {"list", 4},
        {"ab", 2},
        {"abc", 3},
        {"abcd", 4},
        {"cal", 3},   /* 选中此项并按键：执行 LIS2DH12 校准并保存到 EEPROM */
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
        ui_show(pu8g2);
}

static void loop1(u8g2_t *pu8g2) __attribute__((unused));
static void loop1(u8g2_t *pu8g2)
{
        key_scan();
        ui_proc(pu8g2);
}

void ui_test(u8g2_t *pu8g2)
{
#if OLED_RAW_TEST_MODE
        oled_raw_white_test(pu8g2);
#else
        u8g2Init(pu8g2);
        /* Force a small ASCII font to avoid pulling in larger default fonts. */
        u8g2_SetFont(pu8g2, u8g2_font_u8glib_4_tf);
        delay_ms(1000);
        oled_minimal_test(pu8g2);
        delay_ms(1500);
        frame_len = frame_len_trg = list[ui_select].len * 12;
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
        frame_y = frame_y_trg = init_sel * 15;
        frame_len = frame_len_trg = list[init_sel].len * 12;
        ui_flag = (init_sel == 0);
        ui_test(&u8g2);
#if OLED_RAW_TEST_MODE
        for (;;) {
                delay_ms(1000);
        }
#else
        for (;;) {
                loop1(&u8g2);
                delay_ms(500);
        }
#endif
}
