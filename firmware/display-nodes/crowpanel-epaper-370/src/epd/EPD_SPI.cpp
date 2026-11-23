#include "EPD_SPI.h"

void EPD_GPIOInit(void)
{
  pinMode(EPD_PIN_SCK, OUTPUT);
  pinMode(EPD_PIN_MOSI, OUTPUT);
  pinMode(EPD_PIN_RES, OUTPUT);
  pinMode(EPD_PIN_DC, OUTPUT);
  pinMode(EPD_PIN_CS, OUTPUT);
  pinMode(EPD_PIN_BUSY, INPUT);
}

/**
   @brief       IO模拟SPI发送一个字节数据
   @param       dat: 需要发送的字节数据
   @retval      无
*/
void EPD_WR_Bus(uint8_t dat)
{
  uint8_t i;
  EPD_CS_Clr();
  for (i = 0; i < 8; i++)
  {
    EPD_SCK_Clr();
    if (dat & 0x80)
    {
      EPD_MOSI_Set();
    }
    else
    {
      EPD_MOSI_Clr();
    }
    EPD_SCK_Set();
    dat <<= 1;
  }
  EPD_CS_Set();
}

/**
   @brief       向液晶写寄存器命令
   @param       reg: 要写的命令
   @retval      无
*/
void EPD_WR_REG(uint8_t reg)
{
  EPD_DC_Clr();
  EPD_WR_Bus(reg);
  EPD_DC_Set();
}

/**
   @brief       向液晶写一个字节数据
   @param       dat: 要写的数据
   @retval      无
*/
void EPD_WR_DATA8(uint8_t dat)
{
  EPD_DC_Set();
  EPD_WR_Bus(dat);
  EPD_DC_Set();
}
