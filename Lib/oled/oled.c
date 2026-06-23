#include "oled.h"
#include "ui_assets.h"

void* memcpy_byte(void* dst, const void* src, int n)
{
    if (dst == NULL || src == NULL || n <= 0)
        return NULL;

    char* pdst = (char*)dst;
    char* psrc = (char*)src;

    if (pdst > psrc && pdst < psrc + n) {
        /*Memory overlap between pdst and psrc Pointers*/
        pdst = pdst + n - 1;
        psrc = psrc + n - 1;
        while (n--)
            *pdst-- = *psrc--;
    } else {
        while (n--)
            *pdst++ = *psrc++;
    }
    return dst;
}

static uint8_t s_oled_hw_inited;

/* CH1116/SH1106 内供电场景：启用内部 DC-DC，并设置泵电压。 */
static const uint8_t s_oled_internal_dcdc_fix_seq[] = {
        U8X8_START_TRANSFER(),
        U8X8_CA(0x0ad, 0x08b),  /* DC-DC ON, use internal charge pump */
        U8X8_C(0x033),          /* set VPP to high level (datasheet/common demo uses 0x33) */
        U8X8_CA(0x081, 0x0ff),  /* max contrast for first-light test */
        U8X8_C(0x0a4),          /* resume from RAM */
        U8X8_C(0x0a6),          /* normal display */
        U8X8_C(0x0af),          /* display on */
        U8X8_END_TRANSFER(),
        U8X8_END()
};

static HAL_StatusTypeDef oled_spi3_tx(uint8_t *buf, uint16_t len)
{
    const uint32_t timeout_ms = 100U;
    HAL_StatusTypeDef status;

    if (buf == NULL || len == 0U) {
        return HAL_ERROR;
    }

    status = HAL_SPI_Transmit(&hspi3, buf, len, timeout_ms);
    if (status != HAL_OK)
        (void)HAL_SPI_Abort(&hspi3);
    return status;
}

static void oled_hw_init(void)
{
    if (s_oled_hw_inited)
        return;

    MX_SPI3_Init();

    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = OLED_CS_PIN;
    HAL_GPIO_Init(OLED_CS_PORT, &gpio);
    gpio.Pin = OLED_DC_PIN;
    HAL_GPIO_Init(OLED_DC_PORT, &gpio);
    gpio.Pin = OLED_RST_PIN;
    HAL_GPIO_Init(OLED_RST_PORT, &gpio);

    HAL_GPIO_WritePin(OLED_CS_PORT, OLED_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(OLED_RST_PORT, OLED_RST_PIN, GPIO_PIN_RESET);
    delay_ms(10);
    HAL_GPIO_WritePin(OLED_RST_PORT, OLED_RST_PIN, GPIO_PIN_SET);
    delay_ms(10);

    s_oled_hw_inited = 1;
}

uint8_t u8x8_byte_hw_spi3(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr)
{
    static uint8_t spi_err_reported = 0;
    switch (msg) {
        case U8X8_MSG_BYTE_INIT: {
                oled_hw_init();
                /* Keep CS in idle(disabled) level after init, like u8x8 reference drivers. */
                u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
                break;
        }
        case U8X8_MSG_BYTE_START_TRANSFER: {
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
            u8x8_gpio_Delay(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns);
            break;
        }
        case U8X8_MSG_BYTE_SEND: {
            if (oled_spi3_tx((uint8_t *)arg_ptr, (uint16_t)arg_int) != HAL_OK) {
                if (!spi_err_reported) {
                    spi_err_reported = 1;
                    DBG_PRINTF("[oled] SPI3 transmit failed, err=0x%08lx\n", (unsigned long)HAL_SPI_GetError(&hspi3));
                }
                return 0;
            }
            break;
        }
        case U8X8_MSG_BYTE_END_TRANSFER: {
            u8x8_gpio_Delay(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->pre_chip_disable_wait_ns);
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
            break;
        }
        case U8X8_MSG_BYTE_SET_DC: {    
            u8x8_gpio_SetDC(u8x8, arg_int);
            break;
        }
        default:
            return 0;
    }
    return 1;
}


// void delay_us(uint32_t time)
// {
//     uint32_t i = 8 * time;
//     while (i--)
//         ;
// }

uint8_t u8x8_gpio_and_delay(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr)
{
    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            oled_hw_init();
            break;
        case U8X8_MSG_DELAY_100NANO:  // delay arg_int * 100 nano seconds
            if (arg_int > 0) {
                volatile uint32_t n = arg_int;
                while (n--) {
                    __NOP();
                }
            }
            break;
        case U8X8_MSG_DELAY_NANO: {
            volatile uint32_t n = arg_int;
            while (n--) {
                __NOP();
            }
            break;
        }
        case U8X8_MSG_DELAY_10MICRO:  // delay arg_int * 10 micro seconds
            if (arg_int > 0) {
                delay_us((uint32_t)arg_int * 10u);
            }
            break;
        case U8X8_MSG_DELAY_MILLI:  // delay arg_int * 1 milli second
            if (arg_int > 0) {
                delay_ms(arg_int);
            }
            break;
        case U8X8_MSG_DELAY_I2C:  // arg_int is the I2C speed in 100KHz, e.g. 4 = 400 KHz
            if (arg_int == 0) {
                arg_int = 1;
            }
            {
                uint32_t dly_us = 5u / arg_int;
                if (dly_us == 0u) {
                    dly_us = 1u;
                }
                delay_us(dly_us);
            }
            break;                     // arg_int=1: delay by 5us, arg_int = 4: delay by 1.25us
        case U8X8_MSG_GPIO_I2C_CLOCK:  // arg_int=0: Output low at I2C clock pin
            break;                     // arg_int=1: Input dir with pullup high for I2C clock pin
        case U8X8_MSG_GPIO_I2C_DATA:   // arg_int=0: Output low at I2C data pin
            break;                     // arg_int=1: Input dir with pullup high for I2C data pin
        case U8X8_MSG_GPIO_SPI_CLOCK:
            break;
        case U8X8_MSG_GPIO_SPI_DATA:
            break;
        case U8X8_MSG_GPIO_DC:
            HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;
        case U8X8_MSG_GPIO_CS:
            HAL_GPIO_WritePin(OLED_CS_PORT, OLED_CS_PIN, arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;
        case U8X8_MSG_GPIO_RESET:
            HAL_GPIO_WritePin(OLED_RST_PORT, OLED_RST_PIN, arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;
        case U8X8_MSG_GPIO_MENU_SELECT:
            u8x8_SetGPIOResult(u8x8, /* get menu select pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_NEXT:
            u8x8_SetGPIOResult(u8x8, /* get menu next pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_PREV:
            u8x8_SetGPIOResult(u8x8, /* get menu prev pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_HOME:
            u8x8_SetGPIOResult(u8x8, /* get menu home pin state */ 0);
            break;
        default:
            u8x8_SetGPIOResult(u8x8, 1);  // default return value
            break;
    }
    return 1;
}

void u8g2Init(u8g2_t* u8g2)
{
        DBG_PRINTF("[oled] init begin\n");
        /* 使用 u8g2 提供的 SH1106 SPI noname 封装初始化。 */
        /* U8G2_R2: rotate 180° (screen upside down). */
        u8g2_Setup_sh1106_128x64_noname_f(u8g2, U8G2_R2, u8x8_byte_hw_spi3, u8x8_gpio_and_delay);
        DBG_PRINTF("[oled] setup done\n");
        u8g2_InitDisplay(u8g2);
        DBG_PRINTF("[oled] init display done\n");
        u8x8_cad_SendSequence(&u8g2->u8x8, s_oled_internal_dcdc_fix_seq);
        DBG_PRINTF("[oled] dcdc seq done\n");
        u8g2_SetPowerSave(u8g2, 0); 
        u8g2_ClearBuffer(u8g2);
        u8g2_SetContrast(u8g2, 0xFF);
        DBG_PRINTF("[oled] init end\n");
}

static void oled_boot_splash_draw_frame(u8g2_t* u8g2, uint8_t frame)
{
        const uint8_t arc_start = (uint8_t)(frame * 17U);
        const uint8_t arc_end = (uint8_t)(arc_start + 70U);
        const uint8_t pulse_x = (uint8_t)(18U + ((uint16_t)frame * 7U) % 76U);
        const uint8_t dot_phase = (uint8_t)(frame % 3U);

        u8g2_ClearBuffer(u8g2);
        u8g2_DrawXBMP(u8g2, 0, 0, anime_eyes_w, anime_eyes_h, anime_eyes_bits);
        u8g2_DrawCircle(u8g2, 111, 51, 8, U8G2_DRAW_ALL);
        u8g2_DrawArc(u8g2, 111, 51, 8, arc_start, arc_end);
        u8g2_DrawArc(u8g2, 111, 51, 7, arc_start, arc_end);

        for (uint8_t x = 18U; x <= 106U; x = (uint8_t)(x + 4U))
                u8g2_DrawPixel(u8g2, x, 62);
        u8g2_DrawHLine(u8g2, pulse_x, 62, 16);

        for (uint8_t i = 0U; i < 3U; i++) {
                const uint8_t r = (i == dot_phase) ? 2U : 1U;
                u8g2_DrawDisc(u8g2, (u8g2_uint_t)(55U + i * 9U), 56, r, U8G2_DRAW_ALL);
        }
}

void oled_boot_splash_show(u8g2_t* u8g2)
{
        static uint8_t frame;

        if (u8g2 == NULL)
                return;

        /* 开机图：显示转换后的 128x64 XBM 图片。 */
        DBG_PRINTF("[oled] splash begin\n");
        oled_boot_splash_draw_frame(u8g2, frame++);
        u8g2_SendBuffer(u8g2);
        DBG_PRINTF("[oled] splash end\n");
}

void oled_boot_splash_animate(u8g2_t* u8g2, uint16_t duration_ms)
{
        uint16_t elapsed_ms = 0;
        uint8_t frame = 0;
        const uint16_t frame_ms = 70U;

        if (u8g2 == NULL)
                return;

        DBG_PRINTF("[oled] splash animation begin\n");
        do {
                oled_boot_splash_draw_frame(u8g2, frame++);
                u8g2_SendBuffer(u8g2);
                if ((uint16_t)(elapsed_ms + frame_ms) >= duration_ms)
                        break;
                delay_ms(frame_ms);
                elapsed_ms = (uint16_t)(elapsed_ms + frame_ms);
        } while (elapsed_ms < duration_ms);
        DBG_PRINTF("[oled] splash animation end\n");
}

void oled_raw_white_test(u8g2_t* u8g2)
{
        if (u8g2 == NULL)
                return;

        /* Use U8G2 framework for init + draw. */
        u8g2Init(u8g2);
        u8g2_ClearBuffer(u8g2);
        u8g2_DrawBox(u8g2, 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT);
        u8g2_SendBuffer(u8g2);
}
