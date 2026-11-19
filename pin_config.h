#pragma once

// Official pinout for Waveshare ESP32-S3-Touch-AMOLED-1.75C (32 MB Flash, no SD slot)
// Source: github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C (examples/arduino/libraries/board_config/pin_config.h)
#define BOARD_WAVESHARE_175C

#define XPOWERS_CHIP_AXP2101

// 466x466 AMOLED display, CO5300 driver over QSPI
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET 1
#define LCD_TE 13
#define LCD_WIDTH 466
#define LCD_HEIGHT 466

// CST9217 capacitive touch controller over I2C
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT 11
#define TP_RESET 2

// ES8311 audio codec over I2S + NS4150B amplifier
#define I2S_MCK_IO 16
#define I2S_BCK_IO 9
#define I2S_DI_IO 10
#define I2S_WS_IO 45
#define I2S_DO_IO 8
#define PA 46

// Note: ESP32-S3-Touch-AMOLED-1.75C has no microSD slot (GPIO 1 and GPIO 2 are
// wired to LCD_RESET and TP_RESET). Compressed sprites are stored in internal
// 32 MB Flash (LittleFS).
