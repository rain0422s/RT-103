#ifndef __DISPLAY_H
#define __DISPLAY_H
#include "oled.h"
#include "utils.h"

static const unsigned char u8g_logo_bits[] U8X8_PROGMEM =
{
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0xE0,0x01,0x00,0x00,0xF0,0x01,0x00,0x00,0xF8,0x01,0x00,0x00,0xFC,0x01,0x00,0x00,0xFE,0x01,0x00,
	0x30,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,
	0x3C,0xFE,0xFF,0x00,0x3C,0xFE,0xFF,0x00,0x3C,0xFE,0xFF,0x00,0x3C,0xFE,0x7F,0x00,0x3C,0xFE,0x7F,0x00,0x3C,0xFE,0x7F,0x00,0x3C,0xFE,0x3F,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,
};

void ui_test(u8g2_t u8g2);
void loop1(u8g2_t u8g2);
#endif