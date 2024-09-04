#include "display.h"
#define CHECK_KEY(n)  n?  HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_5) : HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_1);
/*循环输出字符，同时输出两端字符，x轴不断改变实现移动UI
* 第一段移动范围 0-----128-----256
* 第二段移动范围-128-----0------128
*/
void loop2(u8g2_t *u8g2) {

        int y=1;                                              //Adjust displacement speed
        for(int x=0;x<256;x+=y){                              // x+=y ----->Equal x=x+y
                u8g2_ClearBuffer(&u8g2);                      // Flush internal buffer
                u8g2_SetFont(&u8g2, u8g2_font_4x6_tr);        // Set font
                u8g2_DrawStr(&u8g2, x, 10, "Hello World!");
                u8g2_DrawStr(&u8g2, x-128,10,"Hello World!");
                delay_ms(20);                                 // Adjusting movement speed
                u8g2_SendBuffer(&u8g2);                       // transfer internal memory to the displa
        }
}

short frame_len,frame_len_trg;  //Width of the box
short frame_y,frame_y_trg;      //Height of the box
signed  char ui_select = 0;

typedef struct
{
  uint8_t val;
  uint8_t last_val;
}KEY_T;

typedef struct
{
  uint8_t id;
  uint8_t press;
  uint8_t update_flag;
  uint8_t res;
}KEY_MSG;

typedef struct
{
  char* str;
  char  len;
}SETTING_LIST;

SETTING_LIST list[] = 
{
  {"list",4},
  {"ab",2},
  {"abc",3},
  {"abcd",4},
};
short x,x_trg; //"x" for current coordinate, "x_trg" for target coordinate.
short y = 13,y_trg = 10;

KEY_T   key[2] = {0};
KEY_MSG key_msg = {0};

short abs(short i)
{
    if (i<0)
        return ~(--i);
    return i;
}

void key_scan(void)
{
        for(int i = 0;i<2;i++)
        {
                key[i].val  = CHECK_KEY(i);

                if(key[i].val != key[i].last_val)
                {
                        key[i].last_val = key[i].val;
                        if(key[i].val == 0)
                        {
                                key_msg.id = i;
                                key_msg.press = 1;
                                key_msg.update_flag = 1;
                        }
                }
        }
}
int ui_run(short *a,short *a_trg,uint8_t step,uint8_t slow_cnt)
{
        uint8_t temp;
        temp = abs(*a_trg-*a) > slow_cnt ? step : 1;
        if(*a < *a_trg)
        {
        *a += temp;
        }
        else if( *a > *a_trg)
        {
        *a -= temp;    
        }
        else
        {
        return 0;
        }
        return 1;
}

void ui_show(u8g2_t u8g2)
{
        int list_len = sizeof(list) / sizeof(SETTING_LIST);
        u8g2_ClearBuffer(&u8g2);                                //Flush internal buffer
        for(int i = 0 ;i < list_len;i++){
                u8g2_DrawStr(&u8g2,x+2,y+i*18,list[i].str);   
        }
        u8g2_DrawRFrame(&u8g2,x, frame_y,frame_len, 20, 3);
        ui_run(&frame_y,&frame_y_trg,5,4);
        ui_run(&frame_len,&frame_len_trg,10,5);
        //ui_run(&y,&y_trg);
        u8g2_SendBuffer(&u8g2);                                 // transfer internal memory to the displa
}

bool ui_flag = true;

void ui_proc(u8g2_t u8g2){
        int list_len = sizeof(list) / sizeof(SETTING_LIST);
        if(key_msg.update_flag && key_msg.press)
        {
                key_msg.update_flag = 0;
                //if(!key_msg.id || ui_flag)
                if(ui_flag)
                {
                        ui_select++;
                        //y_trg += 10;
                        //if(ui_select >= list_len)
                        if(ui_select == list_len-1)
                        {
                                ui_flag = false;
                                //ui_select = list_len - 1;  
                        }
                }
                else{
                        ui_select--;
                        //y_trg -= 10;
                        //if(ui_select < 0)
                        if(ui_select == 0)
                        {
                                //ui_select = 0;  
                                ui_flag = true;
                        }
                }
                frame_y_trg = ui_select*15;
                frame_len_trg = list[ui_select].len*13;
        }
        ui_show(u8g2);
}

void loop1(u8g2_t u8g2){
        key_scan();
        ui_proc(u8g2);  
}






void draw(u8g2_t *u8g2)
{
        u8g2_ClearBuffer(u8g2);
        u8g2_SetFontMode(u8g2, 1);              /*Font mode selection*/
        u8g2_SetFontDirection(u8g2, 0);         /*Font orientation selection*/
        u8g2_SetFont(u8g2, u8g2_font_inb24_mf); /*Font selection*/
        u8g2_DrawStr(u8g2, 0, 20, "U");

        u8g2_SetFontDirection(u8g2, 1);
        u8g2_SetFont(u8g2, u8g2_font_inb30_mn);
        u8g2_DrawStr(u8g2, 21,8,"8");

        u8g2_SetFontDirection(u8g2, 0);
        u8g2_SetFont(u8g2, u8g2_font_inb24_mf);
        u8g2_DrawStr(u8g2, 51,30,"g");
        u8g2_DrawStr(u8g2, 67,30,"\xb2");

        u8g2_DrawHLine(u8g2, 2, 35, 47);
        u8g2_DrawHLine(u8g2, 3, 36, 47);
        u8g2_DrawVLine(u8g2, 45, 32, 12);
        u8g2_DrawVLine(u8g2, 46, 33, 12);

        u8g2_SetFont(u8g2, u8g2_font_4x6_tr);
        u8g2_DrawStr(u8g2, 1,54,"github.com/olikraus/u8g2");
        u8g2_SendBuffer(u8g2);
}

void loop(u8g2_t *u8g2) {
        u8g2_ClearBuffer(u8g2);
        u8g2_SetFont(u8g2,u8g2_font_ncenB14_tr);
        u8g2_DrawStr(u8g2,0,20,"Hello World!");
        u8g2_SendBuffer(u8g2);
}



void ui_test(u8g2_t u8g2){


        u8g2Init(&u8g2);

        //u8g2_FirstPage(&u8g2);
        delay_ms(1000);
        u8g2_DrawLine(&u8g2, 0,0, 127, 63);//测试：起点：横坐标，纵坐标；终点：横坐标，纵坐标
        u8g2_DrawLine(&u8g2, 127,0, 0, 63);
        u8g2_SendBuffer(&u8g2);
        delay_ms(1000);
        u8g2_ClearBuffer(&u8g2);

        delay_ms(1000);
        u8g2_ClearBuffer(&u8g2);
        u8g2_DrawXBMP(&u8g2, 30, 20, 25, 25, u8g_logo_bits);//显示点赞图案
        u8g2_SendBuffer(&u8g2);


        u8g2_SetFont(&u8g2, u8g2_font_t0_22_mf);//设置字体	
        frame_len = frame_len_trg = list[ui_select].len*13;
}


