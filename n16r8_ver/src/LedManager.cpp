#include "LedManager.h"
#include <math.h>

LedManager::LedManager(int pin)
    : _pin(pin)
    , _pattern(LedPattern::RainbowChase)
    , _userPattern(LedPattern::RainbowChase)
    , _targetColor(0x00FF88)
    , _userColor(0x00FF88)
    , _brightness(45)        // Mức sáng vừa mắt, không chói và tiết kiệm điện
    , _lastUpdateMs(0)
    , _step(0)
{
}

void LedManager::begin() {
    pinMode(_pin, OUTPUT);
    writeRgb(0, 0, 0);
}

void LedManager::setUserPattern(LedPattern pattern, uint32_t color) {
    _userPattern = pattern;
    _userColor = color;
    setPattern(pattern, color);
}

void LedManager::setPattern(LedPattern pattern, uint32_t color) {
    if (_pattern != pattern || (_pattern == LedPattern::Solid && _targetColor != color)) {
        _pattern = pattern;
        _targetColor = color;
        _step = 0;
        _lastUpdateMs = 0;
    }
}

void LedManager::setBrightness(uint8_t brightness) {
    _brightness = brightness;
}

const char* LedManager::patternName(LedPattern pattern) {
    switch (pattern) {
        case LedPattern::RainbowChase: return "rainbow";
        case LedPattern::Breathing:    return "breathing";
        case LedPattern::PoliceStrobe: return "police";
        case LedPattern::BlinkSlow:    return "blink_slow";
        case LedPattern::BlinkFast:    return "blink_fast";
        case LedPattern::TurnSignal:   return "turn_signal";
        case LedPattern::SosFallAlert: return "sos";
        case LedPattern::Solid:        return "solid";
        case LedPattern::Off:
        default:                       return "off";
    }
}

LedPattern LedManager::patternFromName(const String& name) {
    if (name.equalsIgnoreCase("rainbow") || name.equalsIgnoreCase("chase")) return LedPattern::RainbowChase;
    if (name.equalsIgnoreCase("breathing") || name.equalsIgnoreCase("pulse")) return LedPattern::Breathing;
    if (name.equalsIgnoreCase("police") || name.equalsIgnoreCase("strobe")) return LedPattern::PoliceStrobe;
    if (name.equalsIgnoreCase("blink_slow") || name.equalsIgnoreCase("blink")) return LedPattern::BlinkSlow;
    if (name.equalsIgnoreCase("blink_fast")) return LedPattern::BlinkFast;
    if (name.equalsIgnoreCase("turn_signal")) return LedPattern::TurnSignal;
    if (name.equalsIgnoreCase("sos")) return LedPattern::SosFallAlert;
    if (name.equalsIgnoreCase("solid")) return LedPattern::Solid;
    return LedPattern::Off;
}

void LedManager::writeRgb(uint8_t r, uint8_t g, uint8_t b) {
    // Áp dụng tỷ lệ độ sáng tổng thể
    const uint16_t scale = _brightness;
    const uint8_t scaledR = static_cast<uint8_t>((r * scale) / 255);
    const uint8_t scaledG = static_cast<uint8_t>((g * scale) / 255);
    const uint8_t scaledB = static_cast<uint8_t>((b * scale) / 255);

    neopixelWrite(_pin, scaledR, scaledG, scaledB);
}

void LedManager::hsvToRgb(uint16_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s == 0) {
        r = g = b = v;
        return;
    }

    h = h % 360;
    const uint8_t region = h / 60;
    const uint32_t remainder = ((h - (region * 60)) * 255) / 60;

    const uint8_t p = (v * (255 - s)) / 255;
    const uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
    const uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;

    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

void LedManager::updateFromSystem(bool fallDetected, bool navigating, bool turnImminent, bool gpsLocked, bool wifiConnected) {
    (void)gpsLocked;
    (void)wifiConnected;

    // Thứ tự ưu tiên 1 (Khẩn cấp cao nhất): Báo động ngã xe / SOS -> Chớp đỏ dồn dập
    if (fallDetected) {
        setPattern(LedPattern::SosFallAlert, 0xFF0000);
        return;
    }

    // Thứ tự ưu tiên 2: Cảnh báo sắp tới khúc cua / ngã rẽ khi đang dẫn đường -> Chớp vàng xi-nhan
    if (navigating && turnImminent) {
        setPattern(LedPattern::TurnSignal, 0xFF9900);
        return;
    }

    // Bình thường: Giữ nguyên hiệu ứng người dùng đã chọn (Mặc định là RainbowChase - LED đuổi dải quang phổ)
    setPattern(_userPattern, _userColor);
}

void LedManager::loop() {
    const uint32_t now = millis();

    switch (_pattern) {
        case LedPattern::Off:
            writeRgb(0, 0, 0);
            break;

        case LedPattern::Solid: {
            const uint8_t r = (_targetColor >> 16) & 0xFF;
            const uint8_t g = (_targetColor >> 8) & 0xFF;
            const uint8_t b = _targetColor & 0xFF;
            writeRgb(r, g, b);
            break;
        }

        case LedPattern::BlinkSlow: { // 1000ms period (500ms ON / 500ms OFF)
            if (now - _lastUpdateMs >= 50) {
                _lastUpdateMs = now;
                const bool on = (now % 1000) < 500;
                if (on) {
                    writeRgb((_targetColor >> 16) & 0xFF, (_targetColor >> 8) & 0xFF, _targetColor & 0xFF);
                } else {
                    writeRgb(0, 0, 0);
                }
            }
            break;
        }

        case LedPattern::BlinkFast: { // 250ms period (125ms ON / 125ms OFF)
            if (now - _lastUpdateMs >= 25) {
                _lastUpdateMs = now;
                const bool on = (now % 250) < 125;
                if (on) {
                    writeRgb((_targetColor >> 16) & 0xFF, (_targetColor >> 8) & 0xFF, _targetColor & 0xFF);
                } else {
                    writeRgb(0, 0, 0);
                }
            }
            break;
        }

        case LedPattern::Breathing: { // 2000ms breathing curve hình sin êm dịu
            if (now - _lastUpdateMs >= 20) {
                _lastUpdateMs = now;
                const float angle = (float)(now % 2000) / 2000.0f * 6.2831853f;
                const float factor = (1.0f - cosf(angle)) * 0.5f;

                const uint8_t r = static_cast<uint8_t>(((_targetColor >> 16) & 0xFF) * factor);
                const uint8_t g = static_cast<uint8_t>(((_targetColor >> 8) & 0xFF) * factor);
                const uint8_t b = static_cast<uint8_t>((_targetColor & 0xFF) * factor);
                writeRgb(r, g, b);
            }
            break;
        }

        case LedPattern::PoliceStrobe: { // 600ms double-flash police strobe
            if (now - _lastUpdateMs >= 15) {
                _lastUpdateMs = now;
                const uint32_t t = now % 600;
                if (t < 50 || (t >= 100 && t < 150)) {
                    writeRgb(255, 0, 0); // Chớp đỏ kép
                } else if ((t >= 300 && t < 350) || (t >= 400 && t < 450)) {
                    writeRgb(0, 40, 255); // Chớp xanh dương kép
                } else {
                    writeRgb(0, 0, 0);
                }
            }
            break;
        }

        case LedPattern::RainbowChase: { // Hiệu ứng "LED đuổi" chuyển màu dải quang phổ 360 độ mượt mà
            if (now - _lastUpdateMs >= 15) { // Cập nhật mỗi 15ms (~66fps)
                _lastUpdateMs = now;
                _step = (_step + 2) % 360; // Tăng 2 độ mỗi 15ms -> 1 chu kỳ 360 độ mất 2.7s siêu mượt

                uint8_t r, g, b;
                hsvToRgb(_step, 255, 255, r, g, b);
                writeRgb(r, g, b);
            }
            break;
        }

        case LedPattern::TurnSignal: { // Nháy vàng hổ phách 600ms (300ms ON / 300ms OFF)
            if (now - _lastUpdateMs >= 20) {
                _lastUpdateMs = now;
                const bool on = (now % 600) < 300;
                if (on) {
                    writeRgb(255, 140, 0); // Vàng hổ phách Amber
                } else {
                    writeRgb(0, 0, 0);
                }
            }
            break;
        }

        case LedPattern::SosFallAlert: { // Chớp đỏ khẩn cấp cảnh báo té ngã
            if (now - _lastUpdateMs >= 15) {
                _lastUpdateMs = now;
                const uint32_t t = now % 400;
                const bool flash = (t < 60) || (t >= 120 && t < 180);
                if (flash) {
                    writeRgb(255, 0, 0); // Đỏ rực
                } else {
                    writeRgb(0, 0, 0);
                }
            }
            break;
        }
    }
}
