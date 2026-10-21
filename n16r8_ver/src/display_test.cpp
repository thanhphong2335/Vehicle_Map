#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <LovyanGFX.hpp>

#include "DisplayConfig.h"
#include "PinConfig.h"

class MotoMapDisplay : public lgfx::LGFX_Device {
public:
    MotoMapDisplay() {
        // Cấu hình SPI trên ESP32-S3
        auto busConfig = _bus.config();
        busConfig.spi_host = SPI2_HOST;
        busConfig.spi_mode = 0;
        busConfig.freq_write = 40000000;
        busConfig.pin_sclk = PinConfig::kPinSclk;
        busConfig.pin_mosi = PinConfig::kPinMosi;
        busConfig.pin_miso = PinConfig::kPinMiso;
        busConfig.pin_dc = PinConfig::kPinDc;
        busConfig.spi_3wire = true;
        busConfig.dma_channel = SPI_DMA_CH_AUTO;

        _bus.config(busConfig);
        _panel.setBus(&_bus);

        auto panelConfig = _panel.config();
        panelConfig.pin_cs = PinConfig::kPinCs;
        panelConfig.pin_rst = PinConfig::kPinRst;
        panelConfig.memory_width = DisplayConfig::kMemoryWidth;
        panelConfig.memory_height = DisplayConfig::kMemoryHeight;
        panelConfig.panel_width = DisplayConfig::kPanelWidth;
        panelConfig.panel_height = DisplayConfig::kPanelHeight;
        panelConfig.offset_x = DisplayConfig::kOffsetX;
        panelConfig.offset_y = DisplayConfig::kOffsetY;
        panelConfig.invert = true;
        panelConfig.rgb_order = false;

        _panel.config(panelConfig);
        setPanel(&_panel);
    }

private:
    lgfx::Bus_SPI _bus;
    lgfx::Panel_ST7789 _panel;
};

MotoMapDisplay lcd;

void showTextDemo() {
    lcd.fillScreen(TFT_BLACK);

    lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    lcd.setTextSize(3);
    lcd.setCursor(20, 20);
    lcd.print("MotoMap S3");

    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(20, 70);
    lcd.print("ST7789 test (N16R8)");

    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setCursor(20, 120);
    lcd.printf("Size: %d x %d | PSRAM: %s", lcd.width(), lcd.height(), psramFound() ? "OK" : "NO");
}

void showShapeDemo() {
    lcd.fillScreen(TFT_BLACK);

    const int16_t maxX = lcd.width() - 1;
    const int16_t maxY = lcd.height() - 1;
    lcd.drawRect(0, 0, maxX, maxY, TFT_WHITE);

    constexpr int16_t marker = 10;
    lcd.fillRect(marker, marker, 6, 6, TFT_RED);
    lcd.fillRect(maxX - marker - 5, marker, 6, 6, TFT_GREEN);
    lcd.fillRect(marker, maxY - marker - 5, 6, 6, TFT_BLUE);
    lcd.fillRect(maxX - marker - 5, maxY - marker - 5, 6, 6, TFT_YELLOW);

    lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setCursor(12, 35);
    lcd.printf("Bounds: 0,0 - %d,%d", maxX, maxY);

    lcd.fillRect(20, 85, 55, 30, TFT_GREEN);
    lcd.drawCircle(125, 100, 28, TFT_CYAN);
    lcd.fillTriangle(230, 130, 280, 70, 310, 130, TFT_YELLOW);
}

void showNavigationDemo() {
    lcd.fillScreen(TFT_BLACK);

    // Header
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setCursor(8, 8);
    lcd.print("NAVIGATION S3");
    lcd.drawFastHLine(0, 24, 320, TFT_GREEN);

    // Mũi tên đi thẳng
    lcd.fillRect(47, 75, 18, 55, TFT_CYAN);
    lcd.fillTriangle(56, 40, 26, 78, 86, 78, TFT_CYAN);

    // Khoảng cách và tên đường
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(3);
    lcd.setCursor(118, 34);
    lcd.print("250 m");

    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(118, 75);
    lcd.print("Le Loi Street");

    // Các ô thông tin
    lcd.fillRect(116, 108, 76, 38, TFT_DARKGREY);
    lcd.fillRect(196, 108, 58, 38, TFT_DARKGREY);
    lcd.fillRect(258, 108, 62, 38, TFT_DARKGREY);

    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    lcd.setCursor(121, 113); lcd.print("REMAIN");
    lcd.setCursor(201, 113); lcd.print("ETA");
    lcd.setCursor(263, 113); lcd.print("SPEED");

    lcd.setTextColor(TFT_WHITE, TFT_DARKGREY);
    lcd.setCursor(121, 130); lcd.print("2.4 km");
    lcd.setCursor(201, 130); lcd.print("5 min");
    lcd.setCursor(263, 130); lcd.print("35");
}

void setup() {
    Serial.begin(115200);

    if (DisplayConfig::kHasBacklightPin && PinConfig::kPinBl >= 0) {
        pinMode(PinConfig::kPinBl, OUTPUT);
        digitalWrite(PinConfig::kPinBl, HIGH);
    }

    lcd.init();
    lcd.setRotation(1);
    lcd.setColorDepth(16);

    lcd.fillScreen(TFT_RED);   delay(500);
    lcd.fillScreen(TFT_GREEN); delay(500);
    lcd.fillScreen(TFT_BLUE);  delay(500);

    showTextDemo();
}

void loop() {
    static uint32_t lastChange = 0;
    static uint8_t page = 0;

    if (millis() - lastChange < 3000) return;
    lastChange = millis();

    page = (page + 1) % 3;

    if (page == 0) showTextDemo();
    if (page == 1) showShapeDemo();
    if (page == 2) showNavigationDemo();
}
