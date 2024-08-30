#ifndef __CRC8_H
#define __CRC8_H

#include "Stddef.h"
#include "stdio.h"
#include "stdint.h"

#define CRC8_POLYNOMIAL 0x31
#define DIRECT_CRC8
#define TABLE_CRC8

uint8_t CheckCrc8(uint8_t *data, uint8_t nbrOfBytes, uint8_t checksum);
uint8_t CalTableCrc8(uint8_t *data, uint8_t nbrOfBytes, uint8_t checksum);
#endif
