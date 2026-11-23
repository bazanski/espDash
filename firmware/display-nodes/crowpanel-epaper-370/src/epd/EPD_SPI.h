#ifndef _EPD_SPI_H_
#define _EPD_SPI_H_

#include <Arduino.h>

// CrowPanel 3.7" E-Paper (UC8253) SPI & control pins
#define EPD_PIN_SCK  12
#define EPD_PIN_MOSI 11
#define EPD_PIN_RES  47
#define EPD_PIN_DC   46
#define EPD_PIN_CS   45
#define EPD_PIN_BUSY 48

#define EPD_SCK_Clr() digitalWrite(EPD_PIN_SCK, LOW)
#define EPD_SCK_Set() digitalWrite(EPD_PIN_SCK, HIGH)

#define EPD_MOSI_Clr() digitalWrite(EPD_PIN_MOSI, LOW)
#define EPD_MOSI_Set() digitalWrite(EPD_PIN_MOSI, HIGH)

#define EPD_RES_Clr() digitalWrite(EPD_PIN_RES, LOW)
#define EPD_RES_Set() digitalWrite(EPD_PIN_RES, HIGH)

#define EPD_DC_Clr() digitalWrite(EPD_PIN_DC, LOW)
#define EPD_DC_Set() digitalWrite(EPD_PIN_DC, HIGH)

#define EPD_CS_Clr() digitalWrite(EPD_PIN_CS, LOW)
#define EPD_CS_Set() digitalWrite(EPD_PIN_CS, HIGH)

#define EPD_ReadBUSY digitalRead(EPD_PIN_BUSY)

void EPD_GPIOInit(void);
void EPD_WR_Bus(uint8_t dat);
void EPD_WR_REG(uint8_t reg);
void EPD_WR_DATA8(uint8_t dat);

#endif
