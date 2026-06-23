#ifndef __OLED_U8G2_H_
#define __OLED_U8G2_H_


#include "u8g2.h"
#include "utils.h"
#include "spi.h"
#include "gpio.h"
#define CONFIG_SCREEN_HEIGHT 64   // height of screen
#define CONFIG_SCREEN_WIDTH  128  // width of screen

#define SSD1306_ADDRESS 0x78
#define OLED_CS_PORT   GPIOB
#define OLED_CS_PIN    GPIO_PIN_9
#define OLED_DC_PORT   GPIOB
#define OLED_DC_PIN    GPIO_PIN_4
#define OLED_RST_PORT  GPIOB
#define OLED_RST_PIN   GPIO_PIN_8

uint8_t u8x8_byte_hw_spi3(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr);
uint8_t u8x8_gpio_and_delay(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr);
void    u8g2Init(u8g2_t* u8g2);
void    oled_boot_splash_show(u8g2_t* u8g2);
void    oled_boot_splash_animate(u8g2_t* u8g2, uint16_t duration_ms);
void    oled_raw_white_test(u8g2_t* u8g2);

#endif
