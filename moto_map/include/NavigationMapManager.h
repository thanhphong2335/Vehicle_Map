#pragma once

#if defined(MOTOMAP_NAV_MAP)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "MotoMapTypes.h"

class DisplayManager;
class RouteManager;

class NavigationMapManager {
public:
    NavigationMapManager(DisplayManager* display, RouteManager* route);

    void begin();
    void onNavigation(const NavigationData& data);
    void loop(bool internetConnected);

    bool showingMap() const;
    const String& lastError() const;

private:
    struct TileKey {
        int32_t x = 0;
        int32_t y = 0;
        uint8_t zoom = 0;

        TileKey() = default;
        TileKey(int32_t tileX, int32_t tileY, uint8_t tileZoom)
            : x(tileX), y(tileY), zoom(tileZoom) {}
    };

    static constexpr uint8_t kMaximumVisibleTiles = 6;

    DisplayManager* _display;
    RouteManager* _route;
    NavigationData _navigation;
    bool _started = false;
    bool _active = false;
    bool _dirty = false;
    bool _showingMap = false;
    bool _internetConnected = false;
    uint32_t _lastRenderMs = 0;
    uint32_t _fallbackSequence = 0;
    uint32_t _visibleSignature = 0;
    float _stableCourseDeg = 0.0F;
    bool _stableCourseValid = false;
    float _displayCourseDeg = 0.0F;
    bool _displayCourseValid = false;
    uint32_t _gpsLostSinceMs = 0;
    bool _gpsLostNoticeShown = false;
    String _lastError;

    TileKey _failedTile;
    bool _failedTileValid = false;
    uint32_t _failedRetryAtMs = 0;

    portMUX_TYPE _taskMux = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t _downloadTask = nullptr;
    bool _jobPending = false;
    bool _jobActive = false;
    TileKey _jobTile;
    TileKey _activeTile;
    bool _resultPending = false;
    bool _resultOk = false;
    TileKey _resultTile;
    char _resultError[48]{};

    static bool sameTile(const TileKey& left, const TileKey& right);
    static void project(double latitude,
                        double longitude,
                        uint8_t zoom,
                        double& worldX,
                        double& worldY);
    static String tilePath(const TileKey& tile);

    bool collectVisibleTiles(TileKey* tiles,
                             uint8_t& count,
                             TileKey& centerTile,
                             double& viewportWorldX,
                             double& viewportWorldY) const;
    bool renderMap();
    void drawRoute(double viewportWorldX, double viewportWorldY);
    void drawVehicleMarker(int16_t x, int16_t y, float courseDeg);
    void drawNavigationOverlay();
    void drawManeuverIcon(Maneuver maneuver, int16_t x, int16_t y);
    void drawWrappedRoadName(const String& road, int16_t x, int16_t y, int16_t width);
    void showFallback();
    void showGpsLostOverlay();

    bool tileExists(const TileKey& tile) const;
    void purgeCacheExcept(const TileKey* keep, uint8_t keepCount);
    void queueMissingTile(const TileKey* tiles,
                          uint8_t count,
                          const TileKey& centerTile,
                          bool internetConnected);
    bool downloadBusy();
    bool tileBusy(const TileKey& tile);
    bool enqueueTile(const TileKey& tile);
    void processDownloadResult();

    static void downloadTaskThunk(void* parameter);
    void downloadLoop();
    bool downloadTile(const TileKey& tile, String& error);
};

#endif
