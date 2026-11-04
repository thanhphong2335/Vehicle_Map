#pragma once

#include <stdint.h>

// ==============================================================================
// ESP32-S3 DevKit Pinout Configuration for MotoMap
// Important Note for ESP32-S3 N16R8 (Octal Flash / Octal PSRAM):
// Strictly DO NOT use GPIO 26 -> 37 (Dedicated to Flash/PSRAM).
// All pins below can be overridden via -DPIN_... build flags in platformio.ini.
// ==============================================================================

namespace PinConfig {

#ifndef PIN_ST7789_SCLK
#define PIN_ST7789_SCLK 13  // SCL
#endif

#ifndef PIN_ST7789_MOSI
#define PIN_ST7789_MOSI 12  // SDA
#endif

#ifndef PIN_ST7789_RST
#define PIN_ST7789_RST 11   // RST
#endif

#ifndef PIN_ST7789_DC
#define PIN_ST7789_DC 10    // DC
#endif

#ifndef PIN_ST7789_CS
#define PIN_ST7789_CS 9     // CS
#endif

#ifndef PIN_ST7789_MISO
#define PIN_ST7789_MISO -1
#endif

#ifndef PIN_ST7789_BL
#define PIN_ST7789_BL -1   // Backlight tied to VCC (unused)
#endif

#ifndef PIN_GPS_RX
#define PIN_GPS_RX 17  // ESP32-S3 RX <- GPS TX
#endif

#ifndef PIN_GPS_TX
#define PIN_GPS_TX 18  // ESP32-S3 TX -> GPS RX
#endif

#ifndef PIN_BUTTON
#define PIN_BUTTON 0   // BOOT button on ESP32-S3 board (active LOW)
#endif

#ifndef PIN_RGB_LED
#define PIN_RGB_LED 48 // Built-in WS2812 RGB LED on ESP32-S3 DevKitC-1
#endif

// ==============================================================================
// I2C Pinout Configuration (IMU MPU-6050 Fall Detection Sensor)
// Note: Avoid GPIO 9 as it is assigned to PIN_ST7789_CS (SPI Display).
// Default to GPIO 7 for SCL and GPIO 8 for SDA on ESP32-S3 DevKitC-1.
// ==============================================================================
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 8
#endif

#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 7
#endif

constexpr int kPinSclk = PIN_ST7789_SCLK;
constexpr int kPinMosi = PIN_ST7789_MOSI;
constexpr int kPinMiso = PIN_ST7789_MISO;
constexpr int kPinDc   = PIN_ST7789_DC;
constexpr int kPinCs   = PIN_ST7789_CS;
constexpr int kPinRst  = PIN_ST7789_RST;
constexpr int kPinBl   = PIN_ST7789_BL;

constexpr int kGpsRxPin = PIN_GPS_RX;
constexpr int kGpsTxPin = PIN_GPS_TX;

constexpr int kButtonPin = PIN_BUTTON;

constexpr int kRgbLedPin = PIN_RGB_LED;

constexpr int kI2cSdaPin = PIN_I2C_SDA;
constexpr int kI2cSclPin = PIN_I2C_SCL;

} // namespace PinConfig
