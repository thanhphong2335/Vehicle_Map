#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "PinConfig.h"

enum class LedPattern : uint8_t {
    Off,
    Solid,          // Sáng màu tĩnh
    BlinkSlow,      // Nhấp nháy chậm (1Hz)
    BlinkFast,      // Nhấp nháy nhanh (4Hz)
    Breathing,      // Hiệu ứng thở (sáng dần / tối dần theo hình sin)
    PoliceStrobe,   // Chớp kép ưu tiên kiểu cảnh sát (Đỏ - Xanh dương)
    RainbowChase,   // "LED đuổi" chuyển màu dải quang phổ (HSV Color Wheel Chase)
    TurnSignal,     // Nháy vàng cảnh báo chuyển hướng / rẽ
    SosFallAlert    // Chớp đỏ rực khẩn cấp khi xe ngã / SOS
};

class LedManager {
public:
    explicit LedManager(int pin = PinConfig::kRgbLedPin);

    void begin();
    void loop();

    // Thiết lập kiểu hiệu ứng đang chạy (runtime)
    void setPattern(LedPattern pattern, uint32_t color = 0x00FF00);
    LedPattern pattern() const { return _pattern; }

    // Thiết lập hiệu ứng người dùng mong muốn (Default / Base pattern)
    void setUserPattern(LedPattern pattern, uint32_t color = 0);
    LedPattern userPattern() const { return _userPattern; }

    // Chỉnh độ sáng tổng thể (0 - 255). Mặc định 40
    void setBrightness(uint8_t brightness);
    uint8_t brightness() const { return _brightness; }

    // Tự động cập nhật theo trạng thái xe (chỉ ghi đè khi ngã xe SOS hoặc xi-nhan rẽ)
    void updateFromSystem(bool fallDetected, bool navigating, bool turnImminent, bool gpsLocked, bool wifiConnected);

    static const char* patternName(LedPattern pattern);
    static LedPattern patternFromName(const String& name);

private:
    int _pin;
    LedPattern _pattern;
    LedPattern _userPattern;
    uint32_t _targetColor;
    uint32_t _userColor;
    uint8_t _brightness;
    uint32_t _lastUpdateMs;
    uint16_t _step;

    void writeRgb(uint8_t r, uint8_t g, uint8_t b);
    static void hsvToRgb(uint16_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b);
};
