#include "crc8.h"
#define CRC8_POLYNOMIAL 0x31

/*Compute multiple byte CRC8 check values*/
uint8_t cal_bytes_crc8(uint8_t *data, uint8_t len){ 
        uint8_t crc8 = 0x00;                        
        uint8_t i;
        while(len--){
                crc8 ^= *data++;                       
                for (i=8;i>0;--i){        
                        crc8 = ( crc8 & 0x80 )
                        ? (crc8 << 1) ^ CRC8_POLYNOMIAL
                        : (crc8 << 1);
                }
        }
        return crc8 ;
}



/*The look-up table method is used to calculate the CRC value*/
uint8_t cal_crc_table(uint8_t *data, uint8_t len){
        uint8_t crc8 = 0x00;

        while (len--){
                crc8 = crc8_table[crc8 ^ *data++];
        }
        return crc8;
}


uint8_t cal_table_high_first(uint8_t value){
        uint8_t i, crc;

        crc = value;
        for (i=8; i>0; --i){ 
                crc = ( crc & 0x80 )
                ? (crc << 1) ^ CRC8_POLYNOMIAL
                : (crc << 1);
        } 

        return crc;
}

void create_crc_table(void){
        uint16_t i;
        uint8_t j;

        for (i=0; i<=0xFF; i++){
                if (0 == (i%16))
                        printf("\n");

                j = i&0xFF;
                        printf("0x%.2x, ", cal_table_high_first (j));  
        }
}

