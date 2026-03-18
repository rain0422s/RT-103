#include "oled.h"

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

/* CH1116 外供 VCC 场景：关掉内部 DC-DC，保留外部 VPP 供电。 */
static const uint8_t s_oled_external_vcc_fix_seq[] = {
        U8X8_START_TRANSFER(),
        U8X8_CA(0x0ad, 0x08a),  /* DC-DC OFF, use external VCC/VPP */
        U8X8_CA(0x081, 0x0ff),  /* max contrast for first-light test */
        U8X8_C(0x0a4),          /* resume from RAM */
        U8X8_C(0x0a6),          /* normal display */
        U8X8_C(0x0af),          /* display on */
        U8X8_END_TRANSFER(),
        U8X8_END()
};

/* 原始点亮测试：参考中景园官方 STM32F103 SPI 例程的 OLED_Init 命令序列，
 * 使用内置 DC-DC 升压 (0xAD 0x8B, 0x33)，然后整屏点亮 (0xA5)。 */
static const uint8_t s_oled_raw_white_seq[] = {
        U8X8_START_TRANSFER(),
        U8X8_C(0x0ae),          /* display off */
        U8X8_C(0x002),          /* set lower column address (0x02) */
        U8X8_C(0x010),          /* set higher column address (0x10) */
        U8X8_C(0x040),          /* set display start line */
        U8X8_C(0x0b0),          /* set page address (page 0) */
        U8X8_CA(0x081, 0x0cf),  /* contrast control = 0xCF */
        U8X8_C(0x0a1),          /* segment remap */
        U8X8_C(0x0a6),          /* normal / reverse: normal */
        U8X8_CA(0x0a8, 0x03f),  /* multiplex ratio = 1/64 */
        U8X8_CA(0x0ad, 0x08b),  /* set charge pump enable, 0x8B = 内供 VCC */
        U8X8_C(0x033),          /* set VPP (0x30~0x33), datasheet example: 0x33 ~ 9V */
        U8X8_C(0x0c8),          /* COM scan direction */
        U8X8_CA(0x0d3, 0x000),  /* display offset */
        U8X8_CA(0x0d5, 0x080),  /* osc division */
        U8X8_CA(0x0d9, 0x01f),  /* pre-charge period */
        U8X8_CA(0x0da, 0x012),  /* COM pins config */
        U8X8_CA(0x0db, 0x040),  /* vcomh */
        U8X8_C(0x0a4),          /* output follows RAM */
        U8X8_C(0x0af),          /* display ON */
        U8X8_C(0x0a5),          /* entire display ON (all pixels lit) */
        U8X8_END_TRANSFER(),
        U8X8_END()
};

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
    switch (msg) {
        case U8X8_MSG_BYTE_INIT: {
                oled_hw_init();
                break;
        }
        case U8X8_MSG_BYTE_START_TRANSFER: {
            u8x8_gpio_SetCS(u8x8, 0);
            u8x8_gpio_Delay(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns);
            break;
        }
        case U8X8_MSG_BYTE_SEND: {
            if (HAL_SPI_Transmit(&hspi3, (uint8_t *)arg_ptr, arg_int, 1000) != HAL_OK)
                return 0;
            break;
        }
        case U8X8_MSG_BYTE_END_TRANSFER: {
            u8x8_gpio_Delay(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->pre_chip_disable_wait_ns);
            u8x8_gpio_SetCS(u8x8, 1);
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
            __NOP();
            break;
        case U8X8_MSG_DELAY_NANO: {
            volatile uint32_t n = arg_int;
            while (n--) {
                __NOP();
            }
            break;
        }
        case U8X8_MSG_DELAY_10MICRO:  // delay arg_int * 10 micro seconds
            for (uint16_t n = 0; n < 320; n++) {
                __NOP();
            }
            break;
        case U8X8_MSG_DELAY_MILLI:  // delay arg_int * 1 milli second
            delay_ms(1);
            break;
        case U8X8_MSG_DELAY_I2C:  // arg_int is the I2C speed in 100KHz, e.g. 4 = 400 KHz
            delay_us(5);
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
        /* CH1116 外供 VCC：先用 SH1106/Winstar 兼容初始化，再覆盖成外供电源参数。 */
        uint8_t tile_buf_height;
        uint8_t *buf = u8g2_m_16_8_f(&tile_buf_height);

        u8g2_SetupDisplay(u8g2, u8x8_d_sh1106_128x64_winstar, u8x8_cad_001,
                          u8x8_byte_hw_spi3, u8x8_gpio_and_delay);
        u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
        u8g2_InitDisplay(u8g2);
        u8x8_cad_SendSequence(&u8g2->u8x8, s_oled_external_vcc_fix_seq);
        u8g2_SetPowerSave(u8g2, 0); 
        u8g2_ClearBuffer(u8g2);
        u8g2_SetContrast(u8g2, 0xFF);
}

void oled_minimal_test(u8g2_t* u8g2)
{
        if (u8g2 == NULL)
                return;

        /* 最小点亮测试：只画固定线条和边框，不进入菜单/字体/图片逻辑。 */
        u8g2_ClearBuffer(u8g2);
        u8g2_DrawFrame(u8g2, 0, 0, 128, 64);
        u8g2_DrawLine(u8g2, 0, 0, 127, 63);
        u8g2_DrawLine(u8g2, 0, 63, 127, 0);
        u8g2_DrawHLine(u8g2, 0, 32, 128);
        u8g2_DrawVLine(u8g2, 64, 0, 64);
        u8g2_SendBuffer(u8g2);
}

void oled_raw_white_test(u8g2_t* u8g2)
{
        if (u8g2 == NULL)
                return;

        /* 只做底层初始化，不发送任何绘图缓冲。 */
        oled_hw_init();
        u8g2_SetupDisplay(u8g2, u8x8_d_sh1106_128x64_winstar, u8x8_cad_001,
                          u8x8_byte_hw_spi3, u8x8_gpio_and_delay);
        u8x8_cad_SendSequence(&u8g2->u8x8, s_oled_raw_white_seq);
}
