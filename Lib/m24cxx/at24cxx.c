
#include "at24cxx.h"

/**
 * AT24CXX 能连着读，但不能连着写超过8个字节。所以每次写的时候，需判断首部
 * 所在页是否还有剩余可写字节，中间的就按页填充，尾部写剩余部分。
 * 如器件出现错误后扫描不到地址，需给 AT24CXX 重新上电。
 */


bool at24cxx_read(struct i2c_cli at24cxx ,uint16_t addr, uint8_t* buff, uint16_t len){
        delay_ms(5);
        return soft_i2c_read_mem(
                at24cxx.bus, at24cxx.dev,addr,buff, len, at24cxx.ops);    
}

bool at24cxx_internal_write(struct i2c_cli at24cxx ,uint16_t addr, uint8_t* buff, uint16_t len){
        delay_ms(5);
        return soft_i2c_write_mem(
                at24cxx.bus, at24cxx.dev,addr,buff, len, at24cxx.ops);
}

bool at24cxx_write(struct i2c_cli at24cxx ,uint16_t addr, uint8_t* buff, uint16_t len){

        if(addr+len>AT24CXX_MAX_ADDR) 
                return false;

        // front
        uint8_t cnt = AT24CXX_PAGE_SIZE-addr%8;
        if(cnt > 0){
                if(len<cnt) 
                        cnt = len;

                if(!at24cxx_internal_write(at24cxx,addr,buff,cnt))
                        return false;

                len -= cnt;

                if(!len) 
                        return true;
                addr += cnt;
                buff += cnt;
        }

        // middle
        while(len >= AT24CXX_PAGE_SIZE){
                if (!at24cxx_internal_write(at24cxx ,addr, buff, AT24CXX_PAGE_SIZE))
                        return false;

                addr += AT24CXX_PAGE_SIZE;
                buff += AT24CXX_PAGE_SIZE;
                len  -= AT24CXX_PAGE_SIZE;
        }

        // back
        if (len > 0) 
                return at24cxx_internal_write(at24cxx ,addr, buff, len);

        return true;
}

/**
 * @param wrap_num number of displays per line, range: [1,64]
 */
bool at24cxx_print(struct i2c_cli at24cxx ,uint16_t start, uint16_t len, uint8_t wrap_num){
        uint8_t buff[64];

        if(!wrap_num)
                return false;

        if(wrap_num>64)
                wrap_num = 64;


        uint16_t end = start + len;
        while(start<end){
                // read data to buff
                if(!at24cxx_read(at24cxx,start,buff,wrap_num)){
                        println("fail to read at24cxx [%05d]", start);
                        return false;
                }

                // print pos and len
                printf("[%05d,%05d,%02d] ", start, start + wrap_num, wrap_num);

                // set next reading pos
                start += wrap_num;

                // print buffer
                for(uint8_t j = 0; j < wrap_num; ++j)
                        printf("%5d ", buff[j]);
                println("");

                // count of rest
                if(start + wrap_num > end)
                        wrap_num = end - start;
        }

        println(SEPARATOR60);
        return true;
}