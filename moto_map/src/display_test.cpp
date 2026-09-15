#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "DisplayConfig.h"

class MotoMapDisplay : public lgfx::LGFX_Device {
public:
    MotoMapDisplay() {
        // Cấu hình SPI
        auto busConfig = _bus.config();
        busConfig.spi_host = VSPI_HOST;
        busConfig.spi_mode = 0;
        busConfig.freq_write = 40000000;
        busConfig.pin_sclk = 18;
        busConfig.pin_mosi = 23;
        busConfig.pin_miso = -1;
        busConfig.pin_dc = 25;
        busConfig.spi_3wire = true;
        busConfig.dma_channel = SPI_DMA_CH_AUTO;

        _bus.config(busConfig);
        _panel.setBus(&_bus);

        auto panelConfig = _panel.config();
        panelConfig.pin_cs = 5;
        panelConfig.pin_rst = 17;
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
    lcd.print("MotoMap");

    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(20, 70);
    lcd.print("ST7789 test");

    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setCursor(20, 120);
    lcd.printf("Size: %d x %d", lcd.width(), lcd.height());
}

void showShapeDemo() {
    lcd.fillScreen(TFT_BLACK);

    // Kiểm tra biên thực tế sau khi xoay ngang: X = 0..319, Y = 0..171.
    const int16_t maxX = lcd.width() - 1;
    const int16_t maxY = lcd.height() - 1;
    lcd.drawRect(0, 0, maxX, maxY, TFT_WHITE);

    // Màn có 4 góc bo cong, nên đặt dấu kiểm tra cách mép 10 pixel.
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
    lcd.print("NAVIGATION");
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

    if (DisplayConfig::kHasBacklightPin) {
        pinMode(DisplayConfig::kBacklightPin, OUTPUT);
        digitalWrite(DisplayConfig::kBacklightPin, HIGH);
    }

    lcd.init();
    lcd.setRotation(1);     // Portrait panel -> landscape UI.
    lcd.setColorDepth(16);  // màu RGB565

    // Test từng màu để kiểm tra màn, dây và driver
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
