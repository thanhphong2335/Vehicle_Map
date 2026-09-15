#include "NavigationMapManager.h"

#if defined(MOTOMAP_NAV_MAP)

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>

#include "DisplayConfig.h"
#include "DisplayManager.h"
#include "RouteManager.h"
#include "TextUtils.h"

namespace {
// Native 1x navigation scale: roads and labels remain easy to read on 320x240.
constexpr uint8_t kMapZoom = 16;
constexpr int32_t kTileSize = 256;
constexpr int16_t kMarkerX = DisplayConfig::kWidth / 2;
constexpr int16_t kMarkerY = (DisplayConfig::kHeight / 2) + 8;
constexpr int16_t kCompactBannerHeight = 50;
constexpr int16_t kExpandedBannerHeight = 64;
constexpr uint32_t kMinimumRenderIntervalMs = 650;
constexpr uint32_t kGpsLostNoticeDelayMs = 7000;
constexpr uint32_t kFailedTileRetryMs = 15000;
constexpr uint32_t kDownloadTimeoutMs = 8000;
constexpr size_t kMaximumTileBytes = 120U * 1024U;
constexpr size_t kMaximumMapCacheBytes = 420U * 1024U;
constexpr size_t kFilesystemReserveBytes = 96U * 1024U;
constexpr char kMapDirectory[] = "/map";
constexpr char kTemporaryTilePath[] = "/map/.download.tmp";

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

size_t mapCacheBytes() {
    File directory = LittleFS.open(kMapDirectory);
    if (!directory || !directory.isDirectory()) return 0;
    size_t total = 0;
    File entry = directory.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) total += entry.size();
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    return total;
}

}

NavigationMapManager::NavigationMapManager(DisplayManager* display, RouteManager* route)
    : _display(display), _route(route) {}

void NavigationMapManager::begin() {
    if (_display == nullptr || _route == nullptr) {
        _lastError = "map_dependencies_missing";
        return;
    }
    if (!LittleFS.begin(false)) {
        _lastError = "map_littlefs_unavailable";
        return;
    }
    if (!LittleFS.exists(kMapDirectory) && !LittleFS.mkdir(kMapDirectory)) {
        _lastError = "map_directory_failed";
        return;
    }
    if (LittleFS.exists(kTemporaryTilePath)) LittleFS.remove(kTemporaryTilePath);

    const BaseType_t created = xTaskCreatePinnedToCore(
        downloadTaskThunk,
        "map_tile",
        7168,
        this,
        1,
        &_downloadTask,
        0);
    if (created != pdPASS) {
        _lastError = "map_task_failed";
        return;
    }
    _started = true;
    Serial.printf("Online navigation map ready: CARTO Voyager z%u, cache max %u KB\n",
                  kMapZoom,
                  static_cast<unsigned>(kMaximumMapCacheBytes / 1024U));
}

void NavigationMapManager::onNavigation(const NavigationData& data) {
    _navigation = data;
    _active = data.state != NavigationState::Stop && data.state != NavigationState::Arrived;
    if (data.gpsValid) {
        _gpsLostSinceMs = 0;
        if (_gpsLostNoticeShown) {
            _gpsLostNoticeShown = false;
            _dirty = true;  // Redraw once to remove the warning from the held map.
        }
    } else if (_active && _gpsLostSinceMs == 0) {
        _gpsLostSinceMs = millis();
    }
    // A stopped NEO-6M still reports small position changes. Redrawing the
    // complete 320x240 map for each of those fixes visibly flashes on a
    // classic ESP32 (there is no PSRAM framebuffer), so leave the current map
    // alone until the vehicle is actually moving. New tiles still set _dirty.
    if (_active && (!_showingMap || data.speedKmh >= 2.0F)) {
        _dirty = true;
    }

    if (!_active) {
        _showingMap = false;
        _fallbackSequence = 0;
        _gpsLostSinceMs = 0;
        _gpsLostNoticeShown = false;
        return;
    }

    // NEO-6M course is only trustworthy while moving. At low speed, keep the
    // last measured direction; for a newly started route use the next route
    // segment as a sensible initial arrow direction.
    if (data.speedKmh >= 3.0F) {
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

    // Route requests and the first OSM tiles can take a few seconds. Keep the
    // navigation-map visual language while waiting; the old full-screen arrow
    // UI is deliberately not used here because it looks like the app exited
    // map navigation.
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

    uint32_t signature = 2166136261UL;
    for (uint8_t index = 0; index < visibleCount; ++index) {
        signature ^= static_cast<uint32_t>(visible[index].x);
        signature *= 16777619UL;
        signature ^= static_cast<uint32_t>(visible[index].y);
        signature *= 16777619UL;
        signature ^= visible[index].zoom;
        signature *= 16777619UL;
    }
    if (signature != _visibleSignature) {
        _visibleSignature = signature;
        purgeCacheExcept(visible, visibleCount);
        _dirty = true;
    }

    const uint32_t now = millis();
    if (_dirty && !downloadBusy() &&
        (now - _lastRenderMs >= kMinimumRenderIntervalMs || !_showingMap)) {
        _dirty = false;
        _lastRenderMs = now;
        if (renderMap()) {
            _showingMap = true;
            _fallbackSequence = 0;
        } else {
            // Once a map frame is visible, keep it while the next tiles are
            // downloading instead of replacing it with the text-only screen.
            // This avoids the map/instruction-screen alternation at a tile edge.
            if (_showingMap) {
                _dirty = true;
            } else {
                showFallback();
            }
        }
    }

    // Start the next HTTPS request only after drawing. On a classic ESP32 this
    // prevents the TLS buffers and PNG decoder from competing for RAM.
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
    if (latitude > 85.05112878) latitude = 85.05112878;
    if (latitude < -85.05112878) latitude = -85.05112878;
    while (longitude < -180.0) longitude += 360.0;
    while (longitude >= 180.0) longitude -= 360.0;

    const double worldSize = static_cast<double>(kTileSize) * (1UL << zoom);
    const double latitudeRadians = latitude * DEG_TO_RAD;
    worldX = ((longitude + 180.0) / 360.0) * worldSize;
    worldY = (1.0 - log(tan(latitudeRadians) + (1.0 / cos(latitudeRadians))) / PI) *
             0.5 * worldSize;
}

String NavigationMapManager::tilePath(const TileKey& tile) {
    char path[48];
    snprintf(path,
             sizeof(path),
             "/map/%u_%ld_%ld.png",
             tile.zoom,
             static_cast<long>(tile.x),
             static_cast<long>(tile.y));
    return String(path);
}

bool NavigationMapManager::collectVisibleTiles(TileKey* tiles,
                                               uint8_t& count,
                                               TileKey& centerTile,
                                               double& viewportWorldX,
                                               double& viewportWorldY) const {
    if (tiles == nullptr || !_navigation.gpsValid) return false;

    double centerWorldX = 0.0;
    double centerWorldY = 0.0;
    project(_navigation.latitude,
            _navigation.longitude,
            kMapZoom,
            centerWorldX,
            centerWorldY);
    if (!isfinite(centerWorldX) || !isfinite(centerWorldY)) return false;

    viewportWorldX = centerWorldX - kMarkerX;
    viewportWorldY = centerWorldY - kMarkerY;
    const int32_t minimumRawX = static_cast<int32_t>(floor(viewportWorldX / kTileSize));
    const int32_t maximumRawX = static_cast<int32_t>(floor(
        (viewportWorldX + DisplayConfig::kWidth - 1) / kTileSize));
    const int32_t minimumY = static_cast<int32_t>(floor(viewportWorldY / kTileSize));
    const int32_t maximumY = static_cast<int32_t>(floor(
        (viewportWorldY + DisplayConfig::kHeight - 1) / kTileSize));
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
    double centerWorldX = 0.0;
    double centerWorldY = 0.0;
    project(_navigation.latitude,
            _navigation.longitude,
            kMapZoom,
            centerWorldX,
            centerWorldY);
    const double viewportWorldX = centerWorldX - kMarkerX;
    const double viewportWorldY = centerWorldY - kMarkerY;
    const int32_t minimumRawX = static_cast<int32_t>(floor(viewportWorldX / kTileSize));
    const int32_t maximumRawX = static_cast<int32_t>(floor(
        (viewportWorldX + DisplayConfig::kWidth - 1) / kTileSize));
    const int32_t minimumY = static_cast<int32_t>(floor(viewportWorldY / kTileSize));
    const int32_t maximumY = static_cast<int32_t>(floor(
        (viewportWorldY + DisplayConfig::kHeight - 1) / kTileSize));
    const int32_t tileCount = 1L << kMapZoom;

    // Do not touch the display until the complete visible tile set is cached.
    // A classic ESP32 cannot hold a full 320x240 framebuffer in addition to
    // TLS/PNG buffers. Rendering partial tile sets after fillScreen() exposed
    // a blank frame, which is the visible "flash" on the TFT.
    uint8_t requiredTiles = 0;
    bool allTilesCached = true;
    for (int32_t y = minimumY; y <= maximumY; ++y) {
        if (y < 0 || y >= tileCount) continue;
        for (int32_t rawX = minimumRawX; rawX <= maximumRawX; ++rawX) {
            int32_t wrappedX = rawX % tileCount;
            if (wrappedX < 0) wrappedX += tileCount;
            ++requiredTiles;
            if (!LittleFS.exists(tilePath(TileKey{wrappedX, y, kMapZoom}))) {
                allTilesCached = false;
            }
        }
    }
    if (requiredTiles == 0 || !allTilesCached) return false;

    lgfx::LGFX_Device& canvas = _display->device();
    uint8_t drawnTiles = 0;
    for (int32_t y = minimumY; y <= maximumY; ++y) {
        if (y < 0 || y >= tileCount) continue;
        for (int32_t rawX = minimumRawX; rawX <= maximumRawX; ++rawX) {
            int32_t wrappedX = rawX % tileCount;
            if (wrappedX < 0) wrappedX += tileCount;
            const TileKey tile{wrappedX, y, kMapZoom};
            const String path = tilePath(tile);
            if (!LittleFS.exists(path)) continue;

            const int32_t screenX = static_cast<int32_t>(
                round(rawX * kTileSize - viewportWorldX));
            const int32_t screenY = static_cast<int32_t>(
                round(y * kTileSize - viewportWorldY));
            if (canvas.drawPngFile(LittleFS, path.c_str(), screenX, screenY)) {
                ++drawnTiles;
            } else {
                Serial.printf("Map tile decode failed: %s\n", path.c_str());
                LittleFS.remove(path);
                _lastError = "map_tile_decode_failed";
            }
        }
    }

    if (drawnTiles != requiredTiles) return false;

    drawRoute(viewportWorldX, viewportWorldY);
    drawVehicleMarker(kMarkerX, kMarkerY, _stableCourseValid ? _stableCourseDeg : 0.0F);
    drawNavigationOverlay();
    return true;
}

void NavigationMapManager::drawRoute(double viewportWorldX, double viewportWorldY) {
    const std::vector<RouteManager::RoutePoint>& points = _route->routePoints();
    if (points.size() < 2) return;
    size_t start = _route->nearestRouteIndex();
    if (start + 1 >= points.size()) start = 0;

    lgfx::LGFX_Device& canvas = _display->device();
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
                if (maxX >= -16 && minX <= DisplayConfig::kWidth + 16 &&
                    maxY >= -16 && minY <= DisplayConfig::kHeight + 16 &&
                    abs(x - previousX) < 1024 && abs(y - previousY) < 1024) {
                    canvas.drawWideLine(previousX, previousY, x, y, width, color);
                }
            }
            previousX = x;
            previousY = y;
            previousValid = true;
        }
    };

    drawPass(TFT_BLACK, 8.0F);
    drawPass(TFT_CYAN, 4.0F);
}

void NavigationMapManager::drawVehicleMarker(int16_t x, int16_t y, float courseDeg) {
    lgfx::LGFX_Device& canvas = _display->device();
    if (!_displayCourseValid) {
        _displayCourseDeg = courseDeg;
        _displayCourseValid = true;
    } else {
        // Same smooth, shortest-path heading transition used by the reference
        // Open_source UI. It eliminates the sharp GPS-course jumps.
        const float delta = shortestAngleDelta(_displayCourseDeg, courseDeg);
        if (fabsf(delta) >= 3.0F) {
            _displayCourseDeg = fmodf(_displayCourseDeg + delta * 0.45F + 360.0F, 360.0F);
        }
    }

    const float radians = _displayCourseDeg * DEG_TO_RAD;
    const float directionX = sin(radians);
    const float directionY = -cos(radians);
    const float perpendicularX = -directionY;
    const float perpendicularY = directionX;
    const auto pointX = [&](float forward, float side) {
        return static_cast<int16_t>(round(x + directionX * forward + perpendicularX * side));
    };
    const auto pointY = [&](float forward, float side) {
        return static_cast<int16_t>(round(y + directionY * forward + perpendicularY * side));
    };

    // Concave navigation-arrow silhouette: tip, upper wing, tail notch and
    // lower wing. This is the same visual language as the supplied reference.
    const int16_t tipX = pointX(22.0F, 0.0F);
    const int16_t tipY = pointY(22.0F, 0.0F);
    const int16_t upperX = pointX(-14.0F, -12.0F);
    const int16_t upperY = pointY(-14.0F, -12.0F);
    const int16_t notchX = pointX(-3.0F, 0.0F);
    const int16_t notchY = pointY(-3.0F, 0.0F);
    const int16_t lowerX = pointX(-14.0F, 12.0F);
    const int16_t lowerY = pointY(-14.0F, 12.0F);
    const uint16_t arrowBlue = 0x03BF;

    canvas.fillTriangle(tipX + 2, tipY + 2, upperX + 2, upperY + 2, notchX + 2, notchY + 2, 0x2104);
    canvas.fillTriangle(tipX + 2, tipY + 2, notchX + 2, notchY + 2, lowerX + 2, lowerY + 2, 0x2104);
    canvas.fillTriangle(tipX, tipY, upperX, upperY, notchX, notchY, arrowBlue);
    canvas.fillTriangle(tipX, tipY, notchX, notchY, lowerX, lowerY, arrowBlue);
}

void NavigationMapManager::drawNavigationOverlay() {
    lgfx::LGFX_Device& canvas = _display->device();
    const uint16_t bannerColor = 0x0841;
    const String road = tftSafeText(_navigation.roadName.length()
        ? _navigation.roadName
        : String("Unnamed road"));
    const String instruction = String(maneuverInstruction(_navigation.maneuver)) + " " + road;
    canvas.setTextSize(2);
    const bool needsSecondLine = canvas.textWidth(instruction) > DisplayConfig::kWidth - 78;
    const int16_t bannerHeight = needsSecondLine ? kExpandedBannerHeight : kCompactBannerHeight;
    canvas.fillRect(0, 0, DisplayConfig::kWidth, bannerHeight, bannerColor);
    canvas.drawFastHLine(0, bannerHeight - 1, DisplayConfig::kWidth, TFT_CYAN);
    // A compact banner is 50 px high. Shift the icon up 4 px there so its
    // visual bounds are centred instead of sitting close to the lower edge.
    drawManeuverIcon(_navigation.maneuver, 7, needsSecondLine ? 6 : -3);

    canvas.setTextColor(TFT_CYAN, bannerColor);
    canvas.setTextSize(2);
    canvas.setCursor(72, 5);
    canvas.print(compactDistance(_navigation.distanceToTurnM));

    if (!_navigation.wifiConnected) {
        canvas.setTextColor(TFT_ORANGE, bannerColor);
        canvas.setTextSize(1);
        canvas.setCursor(DisplayConfig::kWidth - 44, 8);
        canvas.print("OFFLINE");
    }
    drawWrappedRoadName(instruction, 72, 27, DisplayConfig::kWidth - 78);

    // Keep a small unobtrusive attribution required by the map providers,
    // without sacrificing map area with a bottom information bar.
    const char* attribution = "OSM/CARTO";
    canvas.setTextSize(1);
    const int16_t attributionWidth = canvas.textWidth(attribution);
    const int16_t attributionX = DisplayConfig::kWidth - attributionWidth;
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(attributionX, DisplayConfig::kHeight - 9);
    canvas.print(attribution);
}

void NavigationMapManager::drawManeuverIcon(Maneuver maneuver, int16_t x, int16_t y) {
    lgfx::LGFX_Device& canvas = _display->device();
    const uint16_t color = TFT_CYAN;
    // The icon has a fixed 48 x 46 px safe area.  The compact banner is only
    // 50 px tall, so keeping the stroke and arrow head inside this box avoids
    // the turn sign protruding below the black/cyan banner.
    const int16_t cx = x + 25;
    const int16_t cy = y + 24;
    switch (maneuver) {
        case Maneuver::Left: {
            const int16_t shaftX = x + 35;
            const int16_t bendY = y + 20;
            canvas.drawWideLine(shaftX, y + 40, shaftX, bendY, 6, color);
            canvas.drawWideLine(shaftX, bendY, x + 20, bendY, 6, color);
            canvas.fillCircle(shaftX, bendY, 3, color);
            canvas.fillTriangle(x + 6, bendY, x + 20, bendY - 9, x + 20, bendY + 9, color);
            break;
        }
        case Maneuver::Right: {
            const int16_t shaftX = x + 16;
            const int16_t bendY = y + 20;
            canvas.drawWideLine(shaftX, y + 40, shaftX, bendY, 6, color);
            canvas.drawWideLine(shaftX, bendY, x + 32, bendY, 6, color);
            canvas.fillCircle(shaftX, bendY, 3, color);
            canvas.fillTriangle(x + 46, bendY, x + 32, bendY - 9, x + 32, bendY + 9, color);
            break;
        }
        case Maneuver::UTurn:
            canvas.drawWideLine(x + 14, cy + 15, x + 14, cy - 8, 6, color);
            canvas.drawWideLine(x + 14, cy - 8, x + 36, cy - 8, 6, color);
            canvas.drawWideLine(x + 36, cy - 8, x + 36, cy + 7, 6, color);
            canvas.fillTriangle(x + 36, cy + 16, x + 27, cy + 4, x + 45, cy + 4, color);
            break;
        case Maneuver::Roundabout:
            canvas.drawCircle(cx, cy, 14, color);
            canvas.drawCircle(cx, cy, 8, color);
            canvas.fillTriangle(cx + 9, cy - 11, cx + 20, cy - 10, cx + 14, cy - 3, color);
            break;
        case Maneuver::Arrive:
            canvas.fillRect(cx - 3, cy - 17, 6, 35, color);
            canvas.fillTriangle(cx + 3, cy - 17, cx + 21, cy - 9, cx + 3, cy - 1, color);
            break;
        case Maneuver::Straight:
        default:
            canvas.fillRect(cx - 3, cy - 2, 7, 26, color);
            canvas.fillTriangle(cx, cy - 19, cx - 12, cy - 2, cx + 12, cy - 2, color);
            break;
    }
}

void NavigationMapManager::drawWrappedRoadName(const String& road,
                                               int16_t x,
                                               int16_t y,
                                               int16_t width) {
    lgfx::LGFX_Device& canvas = _display->device();
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_WHITE, 0x0841);

    String first = road;
    String second;
    while (canvas.textWidth(first) > width) {
        const int separator = first.lastIndexOf(' ');
        if (separator <= 0) break;
        const String moved = first.substring(separator + 1);
        second = second.length() ? moved + " " + second : moved;
        first = first.substring(0, separator);
    }

    auto trimToWidth = [&](String value) {
        bool shortened = false;
        while (value.length() && canvas.textWidth(value + (shortened ? ".." : "")) > width) {
            size_t removeAt = value.length() - 1;
            while (removeAt > 0 &&
                   (static_cast<uint8_t>(value[removeAt]) & 0xC0) == 0x80) {
                --removeAt;
            }
            value.remove(removeAt);
            shortened = true;
        }
        if (shortened) value += "..";
        return value;
    };

    first = trimToWidth(first);
    second = trimToWidth(second);
    canvas.setCursor(x, y);
    canvas.print(first);
    if (second.length()) {
        canvas.setCursor(x, y + 17);
        canvas.print(second);
    }
}

void NavigationMapManager::showFallback() {
    if (_display == nullptr || !_active) return;
    if (_fallbackSequence == _navigation.sequence && !_showingMap) return;

    // This is shown only before the first complete OSM frame can be drawn.
    // Do not call DisplayManager::showNavigation(): that is the legacy
    // text-only screen and made the map appear to disappear while tiles load.
    lgfx::LGFX_Device& canvas = _display->device();
    constexpr uint16_t bannerColor = 0x0841;
    canvas.fillScreen(TFT_BLACK);
    canvas.fillRect(0, 0, DisplayConfig::kWidth, kCompactBannerHeight, bannerColor);
    canvas.drawFastHLine(0, kCompactBannerHeight - 1, DisplayConfig::kWidth, TFT_CYAN);
    // Same compact 50 px banner as the live map HUD: centre the icon vertically.
    drawManeuverIcon(_navigation.maneuver, 7, -3);

    canvas.setTextColor(TFT_CYAN, bannerColor);
    canvas.setTextSize(2);
    canvas.setCursor(72, 5);
    canvas.print(compactDistance(_navigation.distanceToTurnM));
    canvas.setTextColor(TFT_WHITE, bannerColor);
    canvas.setTextSize(1);
    canvas.setCursor(72, 31);
    canvas.print("DANG TAI BAN DO");

    canvas.setTextColor(TFT_CYAN, TFT_BLACK);
    canvas.setTextSize(2);
    const char* message = _route != nullptr && _route->hasRoute()
        ? "DANG TAI OSM..."
        : "DANG TINH TUYEN...";
    const int16_t textWidth = canvas.textWidth(message);
    canvas.setCursor((DisplayConfig::kWidth - textWidth) / 2, 112);
    canvas.print(message);
    canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    canvas.setTextSize(1);
    const char* detail = _internetConnected
        ? "Vui long cho tile tai xong"
        : "Can Wi-Fi de tai ban do";
    const int16_t detailWidth = canvas.textWidth(detail);
    canvas.setCursor((DisplayConfig::kWidth - detailWidth) / 2, 143);
    canvas.print(detail);

    _fallbackSequence = _navigation.sequence;
    _showingMap = false;
}

void NavigationMapManager::showGpsLostOverlay() {
    if (_display == nullptr || _gpsLostNoticeShown) return;

    lgfx::LGFX_Device& canvas = _display->device();
    const int16_t width = 182;
    const int16_t height = 54;
    const int16_t x = (DisplayConfig::kWidth - width) / 2;
    const int16_t y = (DisplayConfig::kHeight - height) / 2;
    canvas.fillRoundRect(x, y, width, height, 8, 0x2104);
    canvas.drawRoundRect(x, y, width, height, 8, TFT_ORANGE);
    canvas.setTextColor(TFT_ORANGE, 0x2104);
    canvas.setTextSize(2);
    canvas.setCursor(x + 42, y + 8);
    canvas.print("MAT GPS");
    canvas.setTextColor(TFT_WHITE, 0x2104);
    canvas.setTextSize(1);
    canvas.setCursor(x + 20, y + 35);
    canvas.print("GIU LAI BAN DO CU");
    _gpsLostNoticeShown = true;
}

bool NavigationMapManager::tileExists(const TileKey& tile) const {
    return LittleFS.exists(tilePath(tile));
}

void NavigationMapManager::purgeCacheExcept(const TileKey* keep, uint8_t keepCount) {
    File directory = LittleFS.open(kMapDirectory);
    if (!directory || !directory.isDirectory()) return;

    String removePaths[16];
    uint8_t removeCount = 0;
    File entry = directory.openNextFile();
    while (entry) {
        String path = entry.name();
        if (!path.startsWith("/")) path = String(kMapDirectory) + "/" + path;
        const bool isTemporary = path == kTemporaryTilePath;
        bool shouldKeep = isTemporary;
        for (uint8_t index = 0; !shouldKeep && index < keepCount; ++index) {
            shouldKeep = path == tilePath(keep[index]);
        }
        if (!entry.isDirectory() && !shouldKeep && removeCount < 16) {
            removePaths[removeCount++] = path;
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();

    for (uint8_t index = 0; index < removeCount; ++index) {
        LittleFS.remove(removePaths[index]);
    }
}

void NavigationMapManager::queueMissingTile(const TileKey* tiles,
                                            uint8_t count,
                                            const TileKey& centerTile,
                                            bool internetConnected) {
    if (!internetConnected || tiles == nullptr || count == 0) return;

    const auto canQueue = [&](const TileKey& tile) {
        if (tileExists(tile) || tileBusy(tile)) return false;
        if (_failedTileValid && sameTile(tile, _failedTile) &&
            static_cast<int32_t>(millis() - _failedRetryAtMs) < 0) {
            return false;
        }
        return true;
    };

    if (canQueue(centerTile) && enqueueTile(centerTile)) return;
    for (uint8_t index = 0; index < count; ++index) {
        if (canQueue(tiles[index]) && enqueueTile(tiles[index])) return;
    }
}

bool NavigationMapManager::tileBusy(const TileKey& tile) {
    bool busy = false;
    portENTER_CRITICAL(&_taskMux);
    busy = (_jobPending && sameTile(_jobTile, tile)) ||
           (_jobActive && sameTile(_activeTile, tile)) ||
           (_resultPending && sameTile(_resultTile, tile));
    portEXIT_CRITICAL(&_taskMux);
    return busy;
}

bool NavigationMapManager::downloadBusy() {
    bool busy = false;
    portENTER_CRITICAL(&_taskMux);
    busy = _jobPending || _jobActive || _resultPending;
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
    char error[48]{};
    portENTER_CRITICAL(&_taskMux);
    if (_resultPending) {
        hasResult = true;
        ok = _resultOk;
        tile = _resultTile;
        strncpy(error, _resultError, sizeof(error) - 1);
        _resultPending = false;
    }
    portEXIT_CRITICAL(&_taskMux);
    if (!hasResult) return;

    if (ok) {
        _dirty = true;
        _failedTileValid = false;
        _lastError = "";
        Serial.printf("Map tile ready: z%u/%ld/%ld\n",
                      tile.zoom,
                      static_cast<long>(tile.x),
                      static_cast<long>(tile.y));
    } else {
        _failedTile = tile;
        _failedTileValid = true;
        _failedRetryAtMs = millis() + kFailedTileRetryMs;
        _lastError = error[0] ? error : "map_tile_download_failed";
        Serial.printf("Map tile failed: z%u/%ld/%ld (%s)\n",
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
        const bool ok = downloadTile(tile, error);
        portENTER_CRITICAL(&_taskMux);
        _jobActive = false;
        _resultTile = tile;
        _resultOk = ok;
        strncpy(_resultError, error.c_str(), sizeof(_resultError) - 1);
        _resultError[sizeof(_resultError) - 1] = '\0';
        _resultPending = true;
        portEXIT_CRITICAL(&_taskMux);
    }
}

bool NavigationMapManager::downloadTile(const TileKey& tile, String& error) {
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
    http.addHeader("User-Agent", "MotoMap-ESP32/1.0");

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

    const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    const size_t requiredBytes = contentLength > 0
        ? static_cast<size_t>(contentLength)
        : kMaximumTileBytes;
    if (freeBytes < requiredBytes + kFilesystemReserveBytes ||
        mapCacheBytes() + requiredBytes > kMaximumMapCacheBytes) {
        error = "map_cache_full";
        http.end();
        return false;
    }

    if (LittleFS.exists(kTemporaryTilePath)) LittleFS.remove(kTemporaryTilePath);
    File output = LittleFS.open(kTemporaryTilePath, "w");
    if (!output) {
        error = "map_tile_open_failed";
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    uint8_t signature[8]{};
    size_t total = 0;
    int remaining = contentLength;
    uint32_t lastDataMs = millis();
    bool readOk = true;
    while (remaining != 0) {
        const size_t available = stream->available();
        if (available > 0) {
            size_t request = available > sizeof(buffer) ? sizeof(buffer) : available;
            if (remaining > 0 && request > static_cast<size_t>(remaining)) {
                request = static_cast<size_t>(remaining);
            }
            const int received = stream->readBytes(buffer, request);
            if (received <= 0) {
                readOk = false;
                error = "map_tile_read_failed";
                break;
            }
            if (total + received > kMaximumTileBytes) {
                readOk = false;
                error = "map_tile_too_large";
                break;
            }
            for (int index = 0; index < received && total + index < sizeof(signature); ++index) {
                signature[total + index] = buffer[index];
            }
            if (output.write(buffer, received) != static_cast<size_t>(received)) {
                readOk = false;
                error = "map_tile_write_failed";
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
    output.close();
    http.end();

    const uint8_t pngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!readOk || total < 64 || memcmp(signature, pngSignature, sizeof(signature)) != 0 ||
        (contentLength >= 0 && total != static_cast<size_t>(contentLength))) {
        LittleFS.remove(kTemporaryTilePath);
        if (!error.length()) error = "map_tile_invalid_png";
        return false;
    }

    const String destination = tilePath(tile);
    if (LittleFS.exists(destination)) LittleFS.remove(destination);
    if (!LittleFS.rename(kTemporaryTilePath, destination)) {
        LittleFS.remove(kTemporaryTilePath);
        error = "map_tile_rename_failed";
        return false;
    }
    return true;
}

#endif
