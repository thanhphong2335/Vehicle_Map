#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "PinConfig.h"

enum class LedPattern : uint8_t {
    Off,
    Solid,
    PoliceStrobe,
    RainbowChase,
    TurnSignal,
    SosFallAlert
};

class LedManager {
public:
    explicit LedManager(int pin = PinConfig::kRgbLedPin);

    void begin();
    void loop();

    void setPattern(LedPattern pattern, uint32_t color = 0x00FF00);
    LedPattern pattern() const { return _pattern; }

    void setUserPattern(LedPattern pattern, uint32_t color = 0x00FF88);
    LedPattern userPattern() const { return _userPattern; }

    void setBrightness(uint8_t brightness);
    uint8_t brightness() const { return _brightness; }

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
