#include "DisplayManager.h"
#include "TextUtils.h"

#include <LittleFS.h>
#include <TJpg_Decoder.h>

namespace {
constexpr int32_t kWidth = DisplayConfig::kWidth;
constexpr int32_t kHeight = DisplayConfig::kHeight;
DisplayManager* gDisplayManager = nullptr;

bool jpegOutput(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t* bitmap) {
    if (gDisplayManager == nullptr || x >= kWidth || y >= kHeight) {
        return false;
    }

    gDisplayManager->device().pushImage(x, y, width, height, bitmap);
    return true;
}

String compactDistance(uint32_t meters) {
    if (meters >= 10000) {
        return String(meters / 1000) + " km";
    }
    if (meters >= 1000) {
        return String(meters / 1000.0F, 1) + " km";
    }
    return String(meters) + " m";
}
}

DisplayManager::Panel::Panel() {
    {
        auto config = _bus.config();
        config.spi_host = VSPI_HOST;
        config.spi_mode = 0;
        config.freq_write = 50000000;
        config.freq_read = 16000000;
        config.spi_3wire = true;
        config.use_lock = true;
        config.dma_channel = SPI_DMA_CH_AUTO;
        config.pin_sclk = 18;
        config.pin_mosi = 23;
        config.pin_miso = -1;
        config.pin_dc = 25;
        _bus.config(config);
        _panel.setBus(&_bus);
    }

    {
        auto config = _panel.config();
        // Dây thực tế của màn MotoMap: CS -> GPIO5, RES -> GPIO17.
        config.pin_cs = 5;
        config.pin_rst = 17;
        config.pin_busy = -1;
        config.memory_width = DisplayConfig::kMemoryWidth;
        config.memory_height = DisplayConfig::kMemoryHeight;
        config.panel_width = DisplayConfig::kPanelWidth;
        config.panel_height = DisplayConfig::kPanelHeight;
        config.offset_x = DisplayConfig::kOffsetX;
        config.offset_y = DisplayConfig::kOffsetY;
        config.offset_rotation = 0;
        config.dummy_read_pixel = 8;
        config.dummy_read_bits = 1;
        config.readable = false;
        config.invert = true;
        config.rgb_order = false;
        config.dlen_16bit = false;
        config.bus_shared = true;
        _panel.config(config);
    }

    setPanel(&_panel);
}

DisplayManager::DisplayManager() = default;

void DisplayManager::begin() {
    if (DisplayConfig::kHasBacklightPin) {
        pinMode(DisplayConfig::kBacklightPin, OUTPUT);
        digitalWrite(DisplayConfig::kBacklightPin, HIGH);
    }

    _display.init();
    _display.setRotation(1);
    _display.setColorDepth(16);
    _display.fillScreen(TFT_BLACK);

    gDisplayManager = this;
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(jpegOutput);

    // LittleFS (and the boot background JPEG) is mounted by MediaManager
    // immediately after this display initialization.  The first idle screen
    // is therefore drawn by ModeController after that mount succeeds.
}

void DisplayManager::setMode(DeviceMode mode) {
    _mode = mode;
    switch (mode) {
        case DeviceMode::Idle: showIdle(); break;
        case DeviceMode::Media: showMediaPlaceholder(); break;
        case DeviceMode::Error: showError("Device error"); break;
        case DeviceMode::Navigation: break;
    }
}

void DisplayManager::drawHeader(const char* title, uint16_t color) {
    _display.fillRect(0, 0, kWidth, 24, TFT_BLACK);
    _display.setTextColor(color, TFT_BLACK);
    _display.setTextSize(1);
    _display.setCursor(8, 8);
    _display.print(title);
    _display.drawFastHLine(0, 24, kWidth, color);
}

void DisplayManager::drawCentered(const String& text,
                                  int32_t y,
                                  uint8_t size,
                                  uint16_t color,
                                  uint16_t background) {
    _display.setTextSize(size);
    _display.setTextColor(color, background);
    const int16_t width = _display.textWidth(text);
    _display.setCursor(max<int32_t>(0, (kWidth - width) / 2), y);
    _display.print(text);
}

void DisplayManager::drawMetric(const String& label,
                                const String& value,
                                int32_t x,
                                int32_t y,
                                int32_t width) {
    _display.fillRect(x, y, width, 38, TFT_DARKGREY);
    _display.setTextSize(1);
    _display.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    _display.setCursor(x + 5, y + 5);
    _display.print(label);
    _display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    _display.setCursor(x + 5, y + 20);
    _display.print(value);
}

void DisplayManager::drawArrow(Maneuver maneuver) {
    constexpr int16_t cx = 56;
    const int16_t cy = kHeight >= 220 ? 112 : 88;
    constexpr uint16_t color = TFT_CYAN;
    _display.fillRect(0, 25, 112, kHeight - 25, TFT_BLACK);

    switch (maneuver) {
        case Maneuver::Left:
            _display.drawWideLine(75, cy + 46, 75, cy - 10, 13, color);
            _display.drawWideLine(75, cy - 10, 32, cy - 10, 13, color);
            _display.fillCircle(75, cy - 10, 6, color);
            _display.fillTriangle(11, cy - 10, 33, cy - 28, 33, cy + 8, color);
            break;
        case Maneuver::Right:
            _display.drawWideLine(37, cy + 46, 37, cy - 10, 13, color);
            _display.drawWideLine(37, cy - 10, 80, cy - 10, 13, color);
            _display.fillCircle(37, cy - 10, 6, color);
            _display.fillTriangle(101, cy - 10, 79, cy - 28, 79, cy + 8, color);
            break;
        case Maneuver::UTurn:
            _display.drawWideLine(30, cy + 30, 30, cy - 15, 10, color);
            _display.drawWideLine(30, cy - 15, 82, cy - 15, 10, color);
            _display.drawWideLine(82, cy - 15, 82, cy + 22, 10, color);
            _display.fillTriangle(82, cy + 38, 66, cy + 12, 98, cy + 12, color);
            break;
        case Maneuver::Roundabout:
            _display.drawCircle(cx, cy, 34, color);
            _display.drawCircle(cx, cy, 20, color);
            _display.fillTriangle(cx + 21, cy - 28, cx + 46, cy - 25, cx + 31, cy - 7, color);
            break;
        case Maneuver::Arrive:
            _display.fillRect(cx - 6, cy - 36, 12, 76, color);
            _display.fillTriangle(cx + 6, cy - 36, cx + 47, cy - 16, cx + 6, cy + 4, color);
            break;
        case Maneuver::Straight:
        default:
            _display.fillRect(cx - 9, cy - 12, 18, 56, color);
            _display.fillTriangle(cx, cy - 48, cx - 30, cy - 10, cx + 30, cy - 10, color);
            break;
    }
}

void DisplayManager::showNavigation(const NavigationData& data) {
    _mode = DeviceMode::Navigation;
    _display.fillScreen(TFT_BLACK);
    drawHeader(data.statusMessage.length() ? data.statusMessage.c_str() : "NAVIGATION", TFT_GREEN);
    drawArrow(data.maneuver);

    _display.setTextColor(TFT_WHITE, TFT_BLACK);
    _display.setTextSize(3);
    _display.setCursor(118, 34);
    _display.print(compactDistance(data.distanceToTurnM));

    String road = tftSafeText(data.roadName.length() ? data.roadName : "Unnamed road");
    while (road.length() > 0 && _display.textWidth(road) > 194) {
        road.remove(road.length() - 1);
    }
    _display.setTextSize(2);
    _display.setTextColor(TFT_YELLOW, TFT_BLACK);
    _display.setCursor(118, 72);
    _display.print(road);

    const int32_t metricY = kHeight >= 220 ? 164 : 105;
    const int32_t statusY = metricY + 43;
    drawMetric("REMAIN", compactDistance(data.remainingDistanceM), 116, metricY, 76);
    drawMetric("ETA", String(data.etaSeconds / 60) + " min", 196, metricY, 58);
    drawMetric("SPEED", String(static_cast<int>(data.speedKmh + 0.5F)) + " km/h", 258, metricY, 62);

    _display.fillRect(116, statusY, 204, kHeight - statusY, TFT_BLACK);
    _display.setTextSize(1);
    _display.setTextColor(data.gpsValid ? TFT_GREEN : TFT_RED, TFT_BLACK);
    _display.setCursor(120, statusY + 8);
    _display.printf("GPS %u", data.satellites);
    _display.setTextColor(data.wifiConnected ? TFT_CYAN : TFT_ORANGE, TFT_BLACK);
    _display.setCursor(190, statusY + 8);
    _display.print(data.wifiConnected ? "Wi-Fi online" : "Wi-Fi offline");
}

void DisplayManager::showIdle(const String& detail) {
    _mode = DeviceMode::Idle;

    // The LittleFS background contains the complete decorative HUD.  Keep
    // this branch intentionally text-only so the artwork stays clean.
    if (drawJpegFile("/ui/setup_background.jpg")) {
        _mode = DeviceMode::Idle;
        const auto drawOverlayText = [this](const String& text,
                                            int16_t x,
                                            int16_t y,
                                            uint8_t size,
                                            uint16_t color) {
            _display.setTextSize(size);
            _display.setTextColor(color);
            _display.setCursor(x, y);
            _display.print(text);
        };

        _display.setTextSize(2);
        _display.setTextColor(0xFD20);
        _display.setCursor(22, 22);
        _display.print("MOTOMAP");
        _display.setTextSize(1);
        _display.setTextColor(0xF81F);
        _display.setCursor(247, 25);
        _display.print("SYS//READY");

        drawOverlayText("WEB:", 24, 45, 1, 0xF81F);
        drawOverlayText(detail, 36, 64, 2, TFT_WHITE);

        drawOverlayText("WIFI:", 32, 91, 1, 0xF81F);
        drawOverlayText("MotoMap-ESP32", 80, 109, 2, TFT_WHITE);

        drawOverlayText("SETUP URL:", 32, 135, 1, 0xF81F);
        drawOverlayText("http://192.168.4.1/", 32, 150, 2, 0xFD20);

        drawOverlayText("PASSWORD:", 32, 179, 1, 0xFB7F);
        drawOverlayText("motomap123", 110, 201, 2, TFT_WHITE);
        return;
    }

    constexpr uint16_t kBackground = 0x1005;
    constexpr uint16_t kCyanNeon = 0xFD20;
    constexpr uint16_t kMagentaNeon = 0xF81F;
    constexpr uint16_t kLilacNeon = 0xFB7F;
    constexpr uint16_t kTrace = 0x780E;

    const auto drawCutFrame = [this](int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color) {
        constexpr int16_t kCut = 7;
        const int16_t right = x + width - 1;
        const int16_t bottom = y + height - 1;
        _display.drawFastHLine(x + kCut, y, width - 2 * kCut, color);
        _display.drawFastHLine(x + kCut, bottom, width - 2 * kCut, color);
        _display.drawFastVLine(x, y + kCut, height - 2 * kCut, color);
        _display.drawFastVLine(right, y + kCut, height - 2 * kCut, color);
        _display.drawLine(x, y + kCut, x + kCut, y, color);
        _display.drawLine(right - kCut, y, right, y + kCut, color);
        _display.drawLine(x, bottom - kCut, x + kCut, bottom, color);
        _display.drawLine(right - kCut, bottom, right, bottom - kCut, color);
    };

    _display.fillScreen(kBackground);
    for (int16_t y = 38; y < kHeight - 10; y += 10) {
        _display.drawFastHLine(16, y, kWidth - 32, 0x1807);
    }
    // Layered warm outlines simulate the pink/orange glow of the reference UI.
    drawCutFrame(4, 4, kWidth - 8, kHeight - 8, 0x500B);
    drawCutFrame(7, 7, kWidth - 14, kHeight - 14, kMagentaNeon);
    drawCutFrame(11, 11, kWidth - 22, kHeight - 22, kCyanNeon);
    _display.fillRect(24, 15, 74, 2, kCyanNeon);
    _display.fillRect(113, 14, 94, 3, kMagentaNeon);
    _display.fillRect(224, 15, 70, 2, kCyanNeon);
    _display.setTextSize(2);
    _display.setTextColor(kCyanNeon, kBackground);
    _display.setCursor(22, 22);
    _display.print("MOTOMAP");
    _display.setTextSize(1);
    _display.setTextColor(kMagentaNeon, kBackground);
    _display.setCursor(247, 25);
    _display.print("SYS//READY");

    _display.setTextColor(kLilacNeon, kBackground);
    _display.setTextSize(1);
    _display.setCursor(22, 48);
    _display.print("LIVE WEB LINK");
    _display.fillRect(22, 61, kWidth - 44, 1, kTrace);
    drawCentered(detail, 66, 1, TFT_WHITE, kBackground);

    drawCutFrame(20, 84, kWidth - 40, 35, kCyanNeon);
    _display.setTextColor(kCyanNeon, kBackground);
    _display.setTextSize(1);
    _display.setCursor(32, 91);
    _display.print("WIFI:");
    _display.drawFastHLine(82, 96, 180, kTrace);
    drawCentered("MotoMap-ESP32", 102, 1, TFT_WHITE, kBackground);

    drawCutFrame(20, 128, kWidth - 40, 35, kMagentaNeon);
    _display.setTextColor(kMagentaNeon, kBackground);
    _display.setTextSize(1);
    _display.setCursor(32, 135);
    _display.print("OPEN THIS IN BROWSER");
    _display.drawFastHLine(148, 140, 114, kTrace);
    drawCentered("http://192.168.4.1/", 146, 1, kCyanNeon, kBackground);

    drawCutFrame(20, 172, kWidth - 40, 40, kLilacNeon);
    _display.setTextColor(kLilacNeon, kBackground);
    _display.setTextSize(1);
    _display.setCursor(32, 179);
    _display.print("WIFI PASSWORD");
    _display.drawFastHLine(122, 184, 140, kTrace);
    drawCentered("motomap123", 192, 1, TFT_WHITE, kBackground);

    for (int16_t x = 34; x <= 58; x += 8) _display.fillRect(x, 220, 3, 3, kCyanNeon);
    for (int16_t x = 262; x <= 286; x += 8) _display.fillRect(x, 220, 3, 3, kMagentaNeon);
    _display.drawFastHLine(82, 221, 156, kTrace);
}

void DisplayManager::showMediaPlaceholder() {
    _mode = DeviceMode::Media;
    _display.fillScreen(TFT_BLACK);
    drawHeader("MEDIA", TFT_MAGENTA);
    const int32_t titleY = kHeight >= 220 ? 94 : 70;
    drawCentered("No saved animation", titleY, 2, TFT_WHITE);
    drawCentered("Upload from MotoMap web", titleY + 42, 1, TFT_LIGHTGREY);
}

void DisplayManager::showError(const String& message) {
    _mode = DeviceMode::Error;
    showStatus("ERROR", message, TFT_RED);
}

void DisplayManager::showStatus(const String& title, const String& detail, uint16_t color) {
    _display.fillScreen(TFT_BLACK);
    drawHeader(title.c_str(), color);
    drawCentered(detail, kHeight >= 220 ? 110 : 76, 2, color);
}

bool DisplayManager::drawJpeg(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return false;
    }
    _mode = DeviceMode::Media;
    TJpgDec.setJpgScale(1);
    _display.startWrite();
    const bool ok = (TJpgDec.drawJpg(0, 0, data, length) == JDR_OK);
    _display.endWrite();
    return ok;
}

bool DisplayManager::drawJpegFile(const String& path) {
    if (!LittleFS.exists(path)) {
        return false;
    }
    _mode = DeviceMode::Media;
    TJpgDec.setJpgScale(1);
    _display.startWrite();
    const bool ok = (TJpgDec.drawFsJpg(0, 0, path, LittleFS) == JDR_OK);
    _display.endWrite();
    return ok;
}

lgfx::LGFX_Device& DisplayManager::device() {
    return _display;
}
