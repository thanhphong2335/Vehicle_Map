#pragma once

#include <stdint.h>

// ==============================================================================
// ESP32-S3 DevKit Pinout Configuration for MotoMap
// Lưu ý quan trọng cho ESP32-S3 N16R8 (Octal Flash / Octal PSRAM):
// Tuyệt đối KHÔNG sử dụng các chân GPIO 26 -> 37 (Dành riêng cho Flash/PSRAM).
// Các chân dưới đây có thể thay đổi bằng -DPIN_... trong platformio.ini nếu cần.
// ==============================================================================

namespace PinConfig {

#ifndef PIN_ST7789_SCLK
#define PIN_ST7789_SCLK 12
#endif

#ifndef PIN_ST7789_MOSI
#define PIN_ST7789_MOSI 11
#endif

#ifndef PIN_ST7789_MISO
#define PIN_ST7789_MISO -1
#endif

#ifndef PIN_ST7789_DC
#define PIN_ST7789_DC 9
#endif

#ifndef PIN_ST7789_CS
#define PIN_ST7789_CS 10
#endif

#ifndef PIN_ST7789_RST
#define PIN_ST7789_RST 8
#endif

#ifndef PIN_ST7789_BL
#define PIN_ST7789_BL 7
#endif

#ifndef PIN_GPS_RX
#define PIN_GPS_RX 17  // ESP32-S3 RX <- GPS TX
#endif

#ifndef PIN_GPS_TX
#define PIN_GPS_TX 18  // ESP32-S3 TX -> GPS RX
#endif

#ifndef PIN_BUTTON
#define PIN_BUTTON 0   // BOOT button trên board ESP32-S3 (nhấn xuống LOW)
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

} // namespace PinConfig
