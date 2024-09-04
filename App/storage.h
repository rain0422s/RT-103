#ifndef __STORAGE_H
#define __STORAGE_H
#include "w25qxx.h"
#include "at24cxx.h"
#include "i2cdev.h"
#include "string.h"
typedef struct {
    int   i[2];
    float f;
} object_t;

void spi_flash_test(W25QxObjectType *w25qx,uint8_t *ID);
void at24_test(void);
#endif