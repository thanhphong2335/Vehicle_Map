#include "NavigationMapManager.h"

#if defined(MOTOMAP_NAV_MAP)

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "DisplayConfig.h"
#include "DisplayManager.h"
#include "RouteManager.h"
#include "TextUtils.h"

namespace {
// Native 1x navigation scale: roads and labels remain easy to read.
constexpr uint8_t kMapZoom = 16;
constexpr int32_t kTileSize = 256;
constexpr int32_t kMapSpriteSize = 400;     // 400x400 map canvas in PSRAM (diagonal coverage)
constexpr int32_t kMapSpriteCenter = 200;   // Center pivot of map sprite

// Marker position on screen (vehicle is placed slightly below center so driver sees ahead)
constexpr int16_t kMarkerX = DisplayConfig::kWidth / 2;
constexpr int16_t kMarkerY = (DisplayConfig::kHeight / 2) + 18;

constexpr int16_t kCompactBannerHeight = 50;
constexpr int16_t kExpandedBannerHeight = 64;

// High refresh rate enabled by ESP32-S3 PSRAM double buffering (no flicker)
constexpr uint32_t kMinimumRenderIntervalMs = 50;
constexpr uint32_t kGpsLostNoticeDelayMs = 7000;
constexpr uint32_t kFailedTileRetryMs = 15000;
constexpr uint32_t kDownloadTimeoutMs = 8000;
constexpr size_t kMaximumTileBytes = 128U * 1024U;

String compactDistance(uint32_t meters) {
    if (meters >= 10000) return String(meters / 1000) + " km";
    if (meters >= 1000) return String(meters / 1000.0F, 1) + " km";
    return String(meters) + " m";
}

const char* maneuverInstruction(Maneuver maneuver) {
    switch (maneuver) {
        case Maneuver::Left: return "Re Trai";
        case Maneuver::Right: return "Re Phai";
        case Maneuver::UTurn: return "Quay Dau";
        case Maneuver::Roundabout: return "Vong Xoay";
        case Maneuver::Arrive: return "Den Noi";
        case Maneuver::Straight:
        default: return "Di Thang";
    }
}

float bearingDegrees(double fromLatitude,
                     double fromLongitude,
                     double toLatitude,
                     double toLongitude) {
    const double fromLatRadians = fromLatitude * DEG_TO_RAD;
    const double toLatRadians = toLatitude * DEG_TO_RAD;
    const double longitudeDelta = (toLongitude - fromLongitude) * DEG_TO_RAD;
    const double y = sin(longitudeDelta) * cos(toLatRadians);
    const double x = cos(fromLatRadians) * sin(toLatRadians) -
                     sin(fromLatRadians) * cos(toLatRadians) * cos(longitudeDelta);
    float result = static_cast<float>(atan2(y, x) * RAD_TO_DEG);
    if (result < 0.0F) result += 360.0F;
    return result;
}

float shortestAngleDelta(float fromDegrees, float toDegrees) {
    float delta = fmodf(toDegrees - fromDegrees + 540.0F, 360.0F) - 180.0F;
    return delta;
}

} // namespace

NavigationMapManager::NavigationMapManager(DisplayManager* display, RouteManager* route)
    : _display(display), _route(route) {}

NavigationMapManager::~NavigationMapManager() {
    if (_downloadTask != nullptr) {
        vTaskDelete(_downloadTask);
        _downloadTask = nullptr;
    }
    if (_downloadBuffer != nullptr) {
        free(_downloadBuffer);
        _downloadBuffer = nullptr;
    }
}

void NavigationMapManager::initPsramSprites() {
    if (_spritesInitialized) return;

    // Allocate 400x400 map canvas in 8MB Octal PSRAM
    _mapSprite.setPsram(true);
    _mapSprite.setColorDepth(16);
    if (!_mapSprite.createSprite(kMapSpriteSize, kMapSpriteSize)) {
        Serial.println("[MAP] ERROR: Failed to allocate _mapSprite in PSRAM!");
    } else {
        _mapSprite.setPivot(kMapSpriteCenter, kMapSpriteCenter);
        Serial.printf("[MAP] _mapSprite created in PSRAM (%dx%d, ~%u KB)\n",
                      kMapSpriteSize, kMapSpriteSize, (kMapSpriteSize * kMapSpriteSize * 2) / 1024);
    }

    // Allocate 320x240 full screen framebuffer in PSRAM
    _frameSprite.setPsram(true);
    _frameSprite.setColorDepth(16);
    if (!_frameSprite.createSprite(DisplayConfig::kWidth, DisplayConfig::kHeight)) {
        Serial.println("[MAP] ERROR: Failed to allocate _frameSprite in PSRAM!");
    } else {
        Serial.printf("[MAP] _frameSprite created in PSRAM (%dx%d, ~%u KB)\n",
                      DisplayConfig::kWidth, DisplayConfig::kHeight,
                      (DisplayConfig::kWidth * DisplayConfig::kHeight * 2) / 1024);
    }

    // Allocate 20 decoded tile slots in PSRAM (pure in-memory cache, zero flash writes)
    for (size_t i = 0; i < kPsramTileCacheSize; ++i) {
        _psramTileCache[i].sprite.setPsram(true);
        _psramTileCache[i].sprite.setColorDepth(16);
        _psramTileCache[i].sprite.createSprite(kTileSize, kTileSize);
        _psramTileCache[i].valid = false;
        _psramTileCache[i].lastAccess = 0;
    }
    Serial.printf("[MAP] Pre-allocated %u tile sprites in PSRAM (~%u MB)\n",
                  kPsramTileCacheSize,
                  (kPsramTileCacheSize * kTileSize * kTileSize * 2) / (1024 * 1024));

    // Allocate PNG download buffer in PSRAM
    _downloadBuffer = static_cast<uint8_t*>(ps_malloc(kMaximumTileBytes));
    if (_downloadBuffer == nullptr) {
        _downloadBuffer = static_cast<uint8_t*>(malloc(kMaximumTileBytes));
    }

    _spritesInitialized = true;
}

bool NavigationMapManager::getCachedTileSprite(const TileKey& tile, lgfx::LGFX_Sprite*& outSprite) {
    for (size_t i = 0; i < kPsramTileCacheSize; ++i) {
        if (_psramTileCache[i].valid && sameTile(_psramTileCache[i].key, tile)) {
            _psramTileCache[i].lastAccess = millis();
            outSprite = &_psramTileCache[i].sprite;
            return true;
        }
    }
    return false;
}

bool NavigationMapManager::isTileInCache(const TileKey& tile) const {
    for (size_t i = 0; i < kPsramTileCacheSize; ++i) {
        if (_psramTileCache[i].valid && sameTile(_psramTileCache[i].key, tile)) {
            return true;
        }
    }
    return false;
}

void NavigationMapManager::begin() {
    if (_display == nullptr || _route == nullptr) {
        _lastError = "map_dependencies_missing";
        return;
    }

    initPsramSprites();

    const BaseType_t created = xTaskCreatePinnedToCore(
        downloadTaskThunk,
        "map_tile",
        8192,
        this,
        1,
        &_downloadTask,
        0);
    if (created != pdPASS) {
        _lastError = "map_task_failed";
        return;
    }
    _started = true;
    Serial.printf("ESP32-S3 Pure-RAM Heading-Up Navigation Map ready: CARTO Voyager z%u, %u RAM tiles cache (No Flash wear)\n",
                  kMapZoom, kPsramTileCacheSize);
}

void NavigationMapManager::onNavigation(const NavigationData& data) {
    const bool posChanged = (data.latitude != _navigation.latitude ||
                             data.longitude != _navigation.longitude);
    const bool courseChanged = (fabsf(data.courseDeg - _stableCourseDeg) >= 1.0F);

    _navigation = data;
    _active = data.state != NavigationState::Stop && data.state != NavigationState::Arrived;

    if (data.gpsValid) {
        _gpsLostSinceMs = 0;
        if (_gpsLostNoticeShown) {
            _gpsLostNoticeShown = false;
            _dirty = true;
        }
    } else if (_active && _gpsLostSinceMs == 0) {
        _gpsLostSinceMs = millis();
    }

    // Dynamic Map: Update course smoothly
    if (data.speedKmh >= 2.0F && data.courseDeg >= 0.0F) {
        _stableCourseDeg = data.courseDeg;
        _stableCourseValid = true;
    } else if (!_stableCourseValid && _route != nullptr && _route->hasRoute()) {
        const std::vector<RouteManager::RoutePoint>& points = _route->routePoints();
        size_t index = _route->nearestRouteIndex();
        if (index + 1 < points.size()) {
            _stableCourseDeg = bearingDegrees(points[index].latitude,
                                              points[index].longitude,
                                              points[index + 1].latitude,
                                              points[index + 1].longitude);
            _stableCourseValid = true;
        }
    }

    // DYNAMIC UPDATE: Whenever GPS position or course changes, trigger redraw immediately!
    if (_active && (posChanged || courseChanged || !_showingMap)) {
        _dirty = true;
    }

    if (!_active) {
        _showingMap = false;
        _fallbackSequence = 0;
        _gpsLostSinceMs = 0;
        _gpsLostNoticeShown = false;
        return;
    }

    if (!_started || (data.gpsValid && !_route->hasRoute())) {
        showFallback();
    }
}

void NavigationMapManager::loop(bool internetConnected) {
    if (!_started) return;
    _internetConnected = internetConnected;
    processDownloadResult();
    if (!_active) return;

    if (!_navigation.gpsValid) {
        if (_gpsLostSinceMs != 0 &&
            millis() - _gpsLostSinceMs >= kGpsLostNoticeDelayMs) {
            showGpsLostOverlay();
        }
        return;
    }

    if (!_route->hasRoute()) {
        showFallback();
        return;
    }

    TileKey visible[kMaximumVisibleTiles];
    uint8_t visibleCount = 0;
    TileKey centerTile;
    double viewportWorldX = 0.0;
    double viewportWorldY = 0.0;
    if (!collectVisibleTiles(visible,
                             visibleCount,
                             centerTile,
                             viewportWorldX,
                             viewportWorldY)) {
        _lastError = "map_projection_failed";
        showFallback();
        return;
    }

    const uint32_t now = millis();
    if (_dirty && (now - _lastRenderMs >= kMinimumRenderIntervalMs || !_showingMap)) {
        _dirty = false;
        _lastRenderMs = now;
        if (renderMap()) {
            _showingMap = true;
            _fallbackSequence = 0;
        } else {
            if (_showingMap) {
                _dirty = true;
            } else {
                showFallback();
            }
        }
    }

    queueMissingTile(visible, visibleCount, centerTile, internetConnected);
}

bool NavigationMapManager::showingMap() const { return _showingMap; }
const String& NavigationMapManager::lastError() const { return _lastError; }

bool NavigationMapManager::sameTile(const TileKey& left, const TileKey& right) {
    return left.zoom == right.zoom && left.x == right.x && left.y == right.y;
}

void NavigationMapManager::project(double latitude,
                                   double longitude,
                                   uint8_t zoom,
                                   double& worldX,
                                   double& worldY) {
    const double clampedLat = latitude < -85.05112878 ? -85.05112878
                            : latitude > 85.05112878  ? 85.05112878
                                                      : latitude;
    const double latRad = clampedLat * DEG_TO_RAD;
    const double n = static_cast<double>(1L << zoom);
    worldX = ((longitude + 180.0) / 360.0) * n * kTileSize;
    worldY = (1.0 - log(tan(latRad) + (1.0 / cos(latRad))) / M_PI) / 2.0 * n * kTileSize;
}

bool NavigationMapManager::collectVisibleTiles(TileKey* tiles,
                                               uint8_t& count,
                                               TileKey& centerTile,
                                               double& viewportWorldX,
                                               double& viewportWorldY) const {
    double centerWorldX = 0.0;
    double centerWorldY = 0.0;
    project(_navigation.latitude,
            _navigation.longitude,
            kMapZoom,
            centerWorldX,
            centerWorldY);
    if (!isfinite(centerWorldX) || !isfinite(centerWorldY)) return false;

    // Viewport for the 400x400 map canvas centered at vehicle position
    viewportWorldX = centerWorldX - kMapSpriteCenter;
    viewportWorldY = centerWorldY - kMapSpriteCenter;
    const int32_t minimumRawX = static_cast<int32_t>(floor(viewportWorldX / kTileSize));
    const int32_t maximumRawX = static_cast<int32_t>(floor((viewportWorldX + kMapSpriteSize - 1) / kTileSize));
    const int32_t minimumY = static_cast<int32_t>(floor(viewportWorldY / kTileSize));
    const int32_t maximumY = static_cast<int32_t>(floor((viewportWorldY + kMapSpriteSize - 1) / kTileSize));
    const int32_t tileCount = 1L << kMapZoom;

    count = 0;
    for (int32_t y = minimumY; y <= maximumY; ++y) {
        if (y < 0 || y >= tileCount) continue;
        for (int32_t rawX = minimumRawX; rawX <= maximumRawX; ++rawX) {
            if (count >= kMaximumVisibleTiles) return false;
            int32_t wrappedX = rawX % tileCount;
            if (wrappedX < 0) wrappedX += tileCount;
            tiles[count++] = TileKey{wrappedX, y, kMapZoom};
        }
    }

    int32_t centerX = static_cast<int32_t>(floor(centerWorldX / kTileSize)) % tileCount;
    if (centerX < 0) centerX += tileCount;
    centerTile = TileKey{
        centerX,
        static_cast<int32_t>(floor(centerWorldY / kTileSize)),
        kMapZoom};
    return count > 0;
}

bool NavigationMapManager::renderMap() {
    initPsramSprites();

    double centerWorldX = 0.0;
    double centerWorldY = 0.0;
    project(_navigation.latitude,
            _navigation.longitude,
            kMapZoom,
            centerWorldX,
            centerWorldY);

    const double viewportWorldX = centerWorldX - kMapSpriteCenter;
    const double viewportWorldY = centerWorldY - kMapSpriteCenter;
    const int32_t minimumRawX = static_cast<int32_t>(floor(viewportWorldX / kTileSize));
    const int32_t maximumRawX = static_cast<int32_t>(floor((viewportWorldX + kMapSpriteSize - 1) / kTileSize));
    const int32_t minimumY = static_cast<int32_t>(floor(viewportWorldY / kTileSize));
    const int32_t maximumY = static_cast<int32_t>(floor((viewportWorldY + kMapSpriteSize - 1) / kTileSize));
    const int32_t tileCount = 1L << kMapZoom;

    // 1. Draw tiles onto _mapSprite (North-Up orientation centered at 200, 200)
    _mapSprite.fillScreen(0x18C3); // Road land tint
    uint8_t drawnTiles = 0;
    for (int32_t y = minimumY; y <= maximumY; ++y) {
        if (y < 0 || y >= tileCount) continue;
        for (int32_t rawX = minimumRawX; rawX <= maximumRawX; ++rawX) {
            int32_t wrappedX = rawX % tileCount;
            if (wrappedX < 0) wrappedX += tileCount;
            const TileKey tile{wrappedX, y, kMapZoom};

            lgfx::LGFX_Sprite* cachedSprite = nullptr;
            if (getCachedTileSprite(tile, cachedSprite) && cachedSprite != nullptr) {
                const int32_t screenX = static_cast<int32_t>(round(rawX * kTileSize - viewportWorldX));
                const int32_t screenY = static_cast<int32_t>(round(y * kTileSize - viewportWorldY));
                cachedSprite->pushSprite(&_mapSprite, screenX, screenY);
                ++drawnTiles;
            }
        }
    }

    // 2. Draw route onto _mapSprite
    drawRoute(viewportWorldX, viewportWorldY);

    // 3. Smooth heading angle
    if (!_displayCourseValid) {
        _displayCourseDeg = _stableCourseValid ? _stableCourseDeg : 0.0F;
        _displayCourseValid = true;
    } else if (_stableCourseValid) {
        const float delta = shortestAngleDelta(_displayCourseDeg, _stableCourseDeg);
        if (fabsf(delta) >= 1.0F) {
            _displayCourseDeg = fmodf(_displayCourseDeg + delta * 0.35F + 360.0F, 360.0F);
        }
    }

    // 4. Rotate _mapSprite into _frameSprite (Heading-Up Navigation: Map rotates under vehicle!)
    _frameSprite.fillScreen(TFT_BLACK);
    _mapSprite.setPivot(kMapSpriteCenter, kMapSpriteCenter);

#if defined(MOTOMAP_HEADING_UP)
    // Rotate counter-clockwise by course angle so heading points UP (12 o'clock)
    const float rotateAngle = fmodf(360.0F - _displayCourseDeg, 360.0F);
    _mapSprite.pushRotateZoom(&_frameSprite, kMarkerX, kMarkerY, rotateAngle, 1.0F, 1.0F);

    // 5. Draw vehicle marker pointing UP
    drawVehicleMarkerHeadingUp(kMarkerX, kMarkerY);
#else
    _mapSprite.pushSprite(&_frameSprite, kMarkerX - kMapSpriteCenter, kMarkerY - kMapSpriteCenter);
#endif

    // 6. Draw Navigation Overlay banner on top of _frameSprite in screen coords
    drawNavigationOverlay();

    // 7. Push entire composed frame from PSRAM to display via DMA in 1 burst (Zero flicker!)
    _frameSprite.pushSprite(&_display->device(), 0, 0);

    return drawnTiles > 0;
}

void NavigationMapManager::drawRoute(double viewportWorldX, double viewportWorldY) {
    const std::vector<RouteManager::RoutePoint>& points = _route->routePoints();
    if (points.size() < 2) return;
    size_t start = _route->nearestRouteIndex();
    if (start + 1 >= points.size()) start = 0;

    const auto drawPass = [&](uint16_t color, float width) {
        bool previousValid = false;
        int32_t previousX = 0;
        int32_t previousY = 0;
        for (size_t index = start; index < points.size(); ++index) {
            double worldX = 0.0;
            double worldY = 0.0;
            project(points[index].latitude, points[index].longitude, kMapZoom, worldX, worldY);
            const int32_t x = static_cast<int32_t>(round(worldX - viewportWorldX));
            const int32_t y = static_cast<int32_t>(round(worldY - viewportWorldY));
            if (previousValid) {
                const int32_t minX = x < previousX ? x : previousX;
                const int32_t maxX = x > previousX ? x : previousX;
                const int32_t minY = y < previousY ? y : previousY;
                const int32_t maxY = y > previousY ? y : previousY;
                if (maxX >= -20 && minX <= kMapSpriteSize + 20 &&
                    maxY >= -20 && minY <= kMapSpriteSize + 20 &&
                    abs(x - previousX) < 1024 && abs(y - previousY) < 1024) {
                    _mapSprite.drawWideLine(previousX, previousY, x, y, width, color);
                }
            }
            previousX = x;
            previousY = y;
            previousValid = true;
        }
    };

    drawPass(0x0841, 8.0F);   // Dark outer outline
    drawPass(TFT_CYAN, 4.0F); // Vibrant inner route line
}

void NavigationMapManager::drawVehicleMarkerHeadingUp(int16_t x, int16_t y) {
    const int16_t tipX = x;
    const int16_t tipY = y - 18;
    const int16_t leftX = x - 12;
    const int16_t leftY = y + 14;
    const int16_t notchX = x;
    const int16_t notchY = y + 5;
    const int16_t rightX = x + 12;
    const int16_t rightY = y + 14;

    const uint16_t shadowColor = 0x18A3;
    const uint16_t arrowBlue = 0x03BF;  // Vibrant navigation blue
    const uint16_t arrowWhite = TFT_WHITE;

    // Drop shadow
    _frameSprite.fillTriangle(tipX + 2, tipY + 2, leftX + 2, leftY + 2, notchX + 2, notchY + 2, shadowColor);
    _frameSprite.fillTriangle(tipX + 2, tipY + 2, notchX + 2, notchY + 2, rightX + 2, rightY + 2, shadowColor);

    // Main arrow body
    _frameSprite.fillTriangle(tipX, tipY, leftX, leftY, notchX, notchY, arrowBlue);
    _frameSprite.fillTriangle(tipX, tipY, notchX, notchY, rightX, rightY, arrowBlue);

    // Highlight center spine
    _frameSprite.drawLine(tipX, tipY, notchX, notchY, arrowWhite);
}

void NavigationMapManager::drawNavigationOverlay() {
    const uint16_t bannerColor = 0x0841;
    const String road = tftSafeText(_navigation.roadName.length()
        ? _navigation.roadName
        : String("Unnamed road"));
    const String instruction = String(maneuverInstruction(_navigation.maneuver)) + " " + road;
    _frameSprite.setTextSize(2);
    const bool needsSecondLine = _frameSprite.textWidth(instruction) > DisplayConfig::kWidth - 78;
    const int16_t bannerHeight = needsSecondLine ? kExpandedBannerHeight : kCompactBannerHeight;

    _frameSprite.fillRect(0, 0, DisplayConfig::kWidth, bannerHeight, bannerColor);
    _frameSprite.drawFastHLine(0, bannerHeight - 1, DisplayConfig::kWidth, TFT_CYAN);
    drawManeuverIcon(_navigation.maneuver, 7, needsSecondLine ? 6 : -3);

    _frameSprite.setTextSize(2);
    _frameSprite.setTextColor(TFT_WHITE, bannerColor);
    _frameSprite.setCursor(56, 6);
    _frameSprite.print(compactDistance(_navigation.distanceToTurnM));

    if (needsSecondLine) {
        _frameSprite.setTextSize(1);
        _frameSprite.setTextColor(TFT_YELLOW, bannerColor);
        _frameSprite.setCursor(56, 26);
        _frameSprite.print(maneuverInstruction(_navigation.maneuver));
        drawWrappedRoadName(road, 56, 40, DisplayConfig::kWidth - 62);
    } else {
        _frameSprite.setTextSize(1);
        _frameSprite.setTextColor(TFT_YELLOW, bannerColor);
        _frameSprite.setCursor(56, 26);
        String singleLine = String(maneuverInstruction(_navigation.maneuver)) + " " + road;
        while (singleLine.length() > 0 && _frameSprite.textWidth(singleLine) > DisplayConfig::kWidth - 62) {
            singleLine.remove(singleLine.length() - 1);
        }
        _frameSprite.print(singleLine);
    }

    // Bottom status strip
    const int16_t bottomY = DisplayConfig::kHeight - 20;
    _frameSprite.fillRect(0, bottomY, DisplayConfig::kWidth, 20, 0x0841);
    _frameSprite.drawFastHLine(0, bottomY, DisplayConfig::kWidth, 0x2104);
    _frameSprite.setTextSize(1);
    _frameSprite.setTextColor(TFT_LIGHTGREY, 0x0841);
    _frameSprite.setCursor(8, bottomY + 5);
    _frameSprite.printf("%s | ETA %u min | %d km/h",
                        compactDistance(_navigation.remainingDistanceM).c_str(),
                        _navigation.etaSeconds / 60,
                        static_cast<int>(_navigation.speedKmh + 0.5F));
}

void NavigationMapManager::drawManeuverIcon(Maneuver maneuver, int16_t x, int16_t y) {
    constexpr uint16_t color = TFT_CYAN;
    const int16_t cx = x + 20;
    const int16_t cy = y + 26;

    switch (maneuver) {
        case Maneuver::Left:
            _frameSprite.drawWideLine(cx + 8, cy + 14, cx + 8, cy - 2, 5, color);
            _frameSprite.drawWideLine(cx + 8, cy - 2, cx - 8, cy - 2, 5, color);
            _frameSprite.fillTriangle(cx - 15, cy - 2, cx - 6, cy - 9, cx - 6, cy + 5, color);
            break;
        case Maneuver::Right:
            _frameSprite.drawWideLine(cx - 8, cy + 14, cx - 8, cy - 2, 5, color);
            _frameSprite.drawWideLine(cx - 8, cy - 2, cx + 8, cy - 2, 5, color);
            _frameSprite.fillTriangle(cx + 15, cy - 2, cx + 6, cy - 9, cx + 6, cy + 5, color);
            break;
        case Maneuver::UTurn:
            _frameSprite.drawWideLine(cx - 8, cy + 10, cx - 8, cy - 6, 4, color);
            _frameSprite.drawWideLine(cx - 8, cy - 6, cx + 8, cy - 6, 4, color);
            _frameSprite.drawWideLine(cx + 8, cy - 6, cx + 8, cy + 6, 4, color);
            _frameSprite.fillTriangle(cx + 8, cy + 12, cx + 3, cy + 4, cx + 13, cy + 4, color);
            break;
        case Maneuver::Roundabout:
            _frameSprite.drawCircle(cx, cy, 11, color);
            _frameSprite.drawCircle(cx, cy, 7, color);
            _frameSprite.fillTriangle(cx + 7, cy - 9, cx + 15, cy - 8, cx + 10, cy - 2, color);
            break;
        case Maneuver::Arrive:
            _frameSprite.fillRect(cx - 3, cy - 14, 6, 28, color);
            _frameSprite.fillTriangle(cx + 3, cy - 14, cx + 16, cy - 6, cx + 3, cy + 2, color);
            break;
        case Maneuver::Straight:
        default:
            _frameSprite.fillRect(cx - 3, cy - 4, 6, 18, color);
            _frameSprite.fillTriangle(cx, cy - 16, cx - 10, cy - 4, cx + 10, cy - 4, color);
            break;
    }
}

void NavigationMapManager::drawWrappedRoadName(const String& road,
                                               int16_t x,
                                               int16_t y,
                                               int16_t width) {
    _frameSprite.setTextSize(1);
    _frameSprite.setTextColor(TFT_WHITE, 0x0841);
    _frameSprite.setCursor(x, y);

    if (_frameSprite.textWidth(road) <= width) {
        _frameSprite.print(road);
        return;
    }

    int split = -1;
    for (int i = 0; i < static_cast<int>(road.length()); ++i) {
        if (road[i] == ' ' && _frameSprite.textWidth(road.substring(0, i)) <= width) {
            split = i;
        }
    }

    if (split > 0) {
        _frameSprite.print(road.substring(0, split));
        _frameSprite.setCursor(x, y + 10);
        String second = road.substring(split + 1);
        while (second.length() > 0 && _frameSprite.textWidth(second) > width) {
            second.remove(second.length() - 1);
        }
        _frameSprite.print(second);
    } else {
        String trimmed = road;
        while (trimmed.length() > 0 && _frameSprite.textWidth(trimmed) > width) {
            trimmed.remove(trimmed.length() - 1);
        }
        _frameSprite.print(trimmed);
    }
}

void NavigationMapManager::showFallback() {
    _showingMap = false;
    _display->device().fillScreen(0x0841);
    _display->device().fillRect(0, 0, DisplayConfig::kWidth, 24, TFT_BLACK);
    _display->device().setTextColor(TFT_CYAN, TFT_BLACK);
    _display->device().setTextSize(1);
    _display->device().setCursor(8, 8);
    _display->device().print("MOTOMAP S3 - NAVIGATING");

    drawManeuverIcon(_navigation.maneuver, 14, 46);
    _display->device().setTextSize(3);
    _display->device().setTextColor(TFT_WHITE, 0x0841);
    _display->device().setCursor(70, 48);
    _display->device().print(compactDistance(_navigation.distanceToTurnM));

    const String road = tftSafeText(_navigation.roadName.length() ? _navigation.roadName : "Unnamed road");
    _display->device().setTextSize(2);
    _display->device().setTextColor(TFT_YELLOW, 0x0841);
    _display->device().setCursor(70, 80);
    _display->device().print(road);

    _display->device().setTextSize(1);
    _display->device().setTextColor(TFT_LIGHTGREY, 0x0841);
    _display->device().setCursor(70, 115);
    _display->device().print("Streaming online map tiles...");
}

void NavigationMapManager::showGpsLostOverlay() {
    if (_gpsLostNoticeShown) return;
    _gpsLostNoticeShown = true;
    _display->device().fillRect(10, DisplayConfig::kHeight - 50, DisplayConfig::kWidth - 20, 36, 0x6000);
    _display->device().drawRect(10, DisplayConfig::kHeight - 50, DisplayConfig::kWidth - 20, 36, TFT_RED);
    _display->device().setTextColor(TFT_WHITE, 0x6000);
    _display->device().setTextSize(1);
    _display->device().setCursor(20, DisplayConfig::kHeight - 42);
    _display->device().print("WARNING: GPS signal lost. Retrying...");
}

void NavigationMapManager::queueMissingTile(const TileKey* tiles,
                                            uint8_t count,
                                            const TileKey& centerTile,
                                            bool internetConnected) {
    if (!internetConnected || downloadBusy()) return;

    // 1. Highest priority: center tile
    if (!isTileInCache(centerTile)) {
        if (!_failedTileValid || !sameTile(_failedTile, centerTile) ||
            millis() >= _failedRetryAtMs) {
            enqueueTile(centerTile);
            return;
        }
    }

    // 2. High priority: remaining visible tiles
    for (uint8_t i = 0; i < count; ++i) {
        if (!isTileInCache(tiles[i])) {
            if (_failedTileValid && sameTile(_failedTile, tiles[i]) &&
                millis() < _failedRetryAtMs) {
                continue;
            }
            enqueueTile(tiles[i]);
            return;
        }
    }

    // 3. Proactive priority: Pre-fetch upcoming tiles along the active route into PSRAM!
    if (_route != nullptr && _route->hasRoute()) {
        const std::vector<RouteManager::RoutePoint>& points = _route->routePoints();
        const size_t nearestIdx = _route->nearestRouteIndex();
        const size_t totalPoints = points.size();
        const size_t lookAheadCount = 1000;
        const size_t maxIdx = (nearestIdx + lookAheadCount < totalPoints)
            ? (nearestIdx + lookAheadCount)
            : totalPoints;

        TileKey lastCheckedTile{-1, -1, 0};
        for (size_t idx = nearestIdx; idx < maxIdx; ++idx) {
            double worldX = 0.0;
            double worldY = 0.0;
            project(points[idx].latitude, points[idx].longitude, kMapZoom, worldX, worldY);
            if (!isfinite(worldX) || !isfinite(worldY)) continue;

            const int32_t tileCount = 1L << kMapZoom;
            int32_t rawX = static_cast<int32_t>(floor(worldX / kTileSize)) % tileCount;
            if (rawX < 0) rawX += tileCount;
            const int32_t rawY = static_cast<int32_t>(floor(worldY / kTileSize));
            if (rawY < 0 || rawY >= tileCount) continue;

            const TileKey aheadTile{rawX, rawY, kMapZoom};
            if (sameTile(lastCheckedTile, aheadTile)) {
                continue;
            }
            lastCheckedTile = aheadTile;

            if (!isTileInCache(aheadTile)) {
                if (_failedTileValid && sameTile(_failedTile, aheadTile) &&
                    millis() < _failedRetryAtMs) {
                    continue;
                }
                enqueueTile(aheadTile);
                return;
            }
        }
    }
}

bool NavigationMapManager::downloadBusy() {
    bool busy = false;
    portENTER_CRITICAL(&_taskMux);
    busy = _jobPending || _jobActive || _resultPending;
    portEXIT_CRITICAL(&_taskMux);
    return busy;
}

bool NavigationMapManager::tileBusy(const TileKey& tile) {
    bool busy = false;
    portENTER_CRITICAL(&_taskMux);
    busy = (_jobPending && sameTile(_jobTile, tile)) ||
           (_jobActive && sameTile(_activeTile, tile));
    portEXIT_CRITICAL(&_taskMux);
    return busy;
}

bool NavigationMapManager::enqueueTile(const TileKey& tile) {
    bool queued = false;
    portENTER_CRITICAL(&_taskMux);
    if (!_jobPending && !_jobActive && !_resultPending) {
        _jobTile = tile;
        _jobPending = true;
        queued = true;
    }
    portEXIT_CRITICAL(&_taskMux);
    return queued;
}

void NavigationMapManager::processDownloadResult() {
    bool hasResult = false;
    bool ok = false;
    TileKey tile;
    size_t size = 0;
    char error[48]{};
    portENTER_CRITICAL(&_taskMux);
    if (_resultPending) {
        hasResult = true;
        ok = _resultOk;
        tile = _resultTile;
        size = _resultSize;
        strncpy(error, _resultError, sizeof(error) - 1);
        _resultPending = false;
    }
    portEXIT_CRITICAL(&_taskMux);
    if (!hasResult) return;

    if (ok && size > 0 && _downloadBuffer != nullptr) {
        // Eviction strategy: Prefer invalid slots, then slots not currently visible on screen
        TileKey currentVisible[kMaximumVisibleTiles];
        uint8_t currentVisCount = 0;
        TileKey currentCenter;
        double currentVx = 0.0, currentVy = 0.0;
        const bool hasVisible = collectVisibleTiles(currentVisible, currentVisCount, currentCenter, currentVx, currentVy);

        size_t lruIndex = 0;
        uint32_t oldestTime = UINT32_MAX;
        bool foundNonVisible = false;

        for (size_t i = 0; i < kPsramTileCacheSize; ++i) {
            if (!_psramTileCache[i].valid) {
                lruIndex = i;
                foundNonVisible = true;
                break;
            }

            bool isVisible = false;
            if (hasVisible) {
                for (uint8_t v = 0; v < currentVisCount; ++v) {
                    if (sameTile(_psramTileCache[i].key, currentVisible[v])) {
                        isVisible = true;
                        break;
                    }
                }
            }

            if (!isVisible) {
                if (!foundNonVisible || _psramTileCache[i].lastAccess < oldestTime) {
                    oldestTime = _psramTileCache[i].lastAccess;
                    lruIndex = i;
                    foundNonVisible = true;
                }
            } else if (!foundNonVisible) {
                if (_psramTileCache[i].lastAccess < oldestTime) {
                    oldestTime = _psramTileCache[i].lastAccess;
                    lruIndex = i;
                }
            }
        }

        CachedTile& slot = _psramTileCache[lruIndex];
        if (slot.sprite.drawPng(_downloadBuffer, size, 0, 0)) {
            slot.key = tile;
            slot.lastAccess = millis();
            slot.valid = true;
            _dirty = true;
            _failedTileValid = false;
            _lastError = "";
            Serial.printf("[MAP] Tile streamed to PSRAM: z%u/%ld/%ld (%u bytes)\n",
                          tile.zoom,
                          static_cast<long>(tile.x),
                          static_cast<long>(tile.y),
                          static_cast<unsigned>(size));
        } else {
            Serial.printf("[MAP] Decode PNG failed in PSRAM for tile: z%u/%ld/%ld\n",
                          tile.zoom, static_cast<long>(tile.x), static_cast<long>(tile.y));
            slot.valid = false;
        }
    } else {
        _failedTile = tile;
        _failedTileValid = true;
        _failedRetryAtMs = millis() + kFailedTileRetryMs;
        _lastError = error[0] ? error : "map_tile_download_failed";
        Serial.printf("[MAP] Tile download failed: z%u/%ld/%ld (%s)\n",
                      tile.zoom,
                      static_cast<long>(tile.x),
                      static_cast<long>(tile.y),
                      _lastError.c_str());
    }
}

void NavigationMapManager::downloadTaskThunk(void* parameter) {
    auto* manager = static_cast<NavigationMapManager*>(parameter);
    manager->downloadLoop();
}

void NavigationMapManager::downloadLoop() {
    for (;;) {
        bool hasJob = false;
        TileKey tile;
        portENTER_CRITICAL(&_taskMux);
        if (_jobPending) {
            tile = _jobTile;
            _jobPending = false;
            _jobActive = true;
            _activeTile = tile;
            hasJob = true;
        }
        portEXIT_CRITICAL(&_taskMux);

        if (!hasJob) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        String error;
        size_t received = 0;
        const bool ok = downloadTile(tile, received, error);
        portENTER_CRITICAL(&_taskMux);
        _jobActive = false;
        _resultTile = tile;
        _resultSize = received;
        _resultOk = ok;
        strncpy(_resultError, error.c_str(), sizeof(_resultError) - 1);
        _resultError[sizeof(_resultError) - 1] = '\0';
        _resultPending = true;
        portEXIT_CRITICAL(&_taskMux);
    }
}

bool NavigationMapManager::downloadTile(const TileKey& tile, size_t& receivedBytes, String& error) {
    receivedBytes = 0;
    if (_downloadBuffer == nullptr) {
        error = "no_download_buffer";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        error = "map_offline";
        return false;
    }

    const char subdomain = static_cast<char>('a' + ((tile.x + tile.y) % 3));
    String url;
    url.reserve(128);
    url += "https://";
    url += subdomain;
    url += ".basemaps.cartocdn.com/rastertiles/voyager/";
    url += String(tile.zoom);
    url += '/';
    url += String(tile.x);
    url += '/';
    url += String(tile.y);
    url += ".png";

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(kDownloadTimeoutMs / 1000U);
    HTTPClient http;
    http.setConnectTimeout(kDownloadTimeoutMs);
    http.setTimeout(kDownloadTimeoutMs);
    http.useHTTP10(true);
    const char* headerKeys[] = {"Content-Type"};
    http.collectHeaders(headerKeys, 1);
    if (!http.begin(client, url)) {
        error = "map_http_begin_failed";
        return false;
    }
    http.addHeader("Accept", "image/png");
    http.addHeader("User-Agent", "MotoMap-S3/1.0");

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        error = String("map_http_") + status;
        http.end();
        return false;
    }

    const String contentType = http.header("Content-Type");
    if (contentType.length() && contentType.indexOf("image/png") < 0) {
        error = "map_not_png";
        http.end();
        return false;
    }
    const int contentLength = http.getSize();
    if (contentLength > static_cast<int>(kMaximumTileBytes)) {
        error = "map_tile_too_large";
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    int remaining = contentLength;
    uint32_t lastDataMs = millis();
    bool readOk = true;

    while (remaining != 0) {
        const size_t available = stream->available();
        if (available > 0) {
            size_t request = available > 1024 ? 1024 : available;
            if (remaining > 0 && request > static_cast<size_t>(remaining)) {
                request = static_cast<size_t>(remaining);
            }
            if (total + request > kMaximumTileBytes) {
                readOk = false;
                error = "map_tile_too_large";
                break;
            }
            const int received = stream->readBytes(_downloadBuffer + total, request);
            if (received <= 0) {
                readOk = false;
                error = "map_tile_read_failed";
                break;
            }
            total += received;
            if (remaining > 0) remaining -= received;
            lastDataMs = millis();
            continue;
        }

        if (contentLength < 0 && !http.connected()) break;
        if (millis() - lastDataMs > kDownloadTimeoutMs) {
            readOk = false;
            error = "map_tile_timeout";
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    http.end();

    const uint8_t pngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!readOk || total < 64 || memcmp(_downloadBuffer, pngSignature, sizeof(pngSignature)) != 0 ||
        (contentLength >= 0 && total != static_cast<size_t>(contentLength))) {
        if (!error.length()) error = "map_tile_invalid_png";
        return false;
    }

    receivedBytes = total;
    return true;
}

#endif
