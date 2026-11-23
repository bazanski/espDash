#pragma once

#include <Arduino.h>

// =========================================================================
// BOARD HARDWARE DEFINITIONS: LCD Wiki ES3C35P (ESP32-S3 3.5" QSPI + TOUCH)
// =========================================================================

// LCD ST77922 (320x480) QSPI
#define LCD_QSPI_HOST       SPI2_HOST
#define LCD_PIN_CS          10
#define LCD_PIN_CLK         12
#define LCD_PIN_D0          11
#define LCD_PIN_D1          13
#define LCD_PIN_D2          14
#define LCD_PIN_D3          9
#define LCD_PIN_RST         -1   // Shared with ESP32 EN/RESET pin
#define LCD_PIN_BL          41   // Backlight PWM / GPIO control (High = ON)

#define LCD_WIDTH           320
#define LCD_HEIGHT          480

// Capacitive Touch Controller (ST77922 built-in TDDI via I2C)
#define TOUCH_I2C_PORT      0
#define TOUCH_PIN_SDA       38
#define TOUCH_PIN_SCL       39
#define TOUCH_PIN_RST       48
#define TOUCH_PIN_INT       47
#define TOUCH_I2C_ADDR      0x55

// Onboard WS2812B RGB LED
#define RGB_LED_PIN         40

// Battery & Power Management
#define BATTERY_ADC_PIN     8    // TP4054 battery voltage measurement

// Onboard Buttons
#define BOOT_BTN_PIN        0

// Extended Headers
#define EXT_IO45_PIN        45
#define EXT_IO46_PIN        46

// Serial Port (UART0)
#define UART_TXD_PIN        43
#define UART_RXD_PIN        44
