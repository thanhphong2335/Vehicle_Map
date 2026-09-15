#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "DisplayConfig.h"
#include "MotoMapTypes.h"

class DisplayManager {
public:
    DisplayManager();

    void begin();
    void setMode(DeviceMode mode);
    void showNavigation(const NavigationData& data);
    void showIdle(const String& detail = "Waiting for iPhone");
    void showMediaPlaceholder();
    void showError(const String& message);
    void showStatus(const String& title, const String& detail, uint16_t color = TFT_CYAN);
    bool drawJpeg(const uint8_t* data, size_t length);
    bool drawJpegFile(const String& path);
    int32_t width() const { return DisplayConfig::kWidth; }
    int32_t height() const { return DisplayConfig::kHeight; }

    lgfx::LGFX_Device& device();

private:
    class Panel : public lgfx::LGFX_Device {
    public:
        Panel();
    private:
        lgfx::Bus_SPI _bus;
        lgfx::Panel_ST7789 _panel;
    };

    Panel _display;
    DeviceMode _mode = DeviceMode::Idle;

    void drawHeader(const char* title, uint16_t color);
    void drawArrow(Maneuver maneuver);
    void drawCentered(const String& text, int32_t y, uint8_t size, uint16_t color,
                      uint16_t background = TFT_BLACK);
    void drawMetric(const String& label, const String& value, int32_t x, int32_t y, int32_t width);
};
