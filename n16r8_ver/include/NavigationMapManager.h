#pragma once

#if defined(MOTOMAP_NAV_MAP)

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <LovyanGFX.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "MotoMapTypes.h"

class DisplayManager;
class RouteManager;

class NavigationMapManager {
public:
    NavigationMapManager(DisplayManager* display, RouteManager* route);
    ~NavigationMapManager();

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

    struct CachedTile {
        TileKey key;
        lgfx::LGFX_Sprite sprite;
        uint32_t lastAccess = 0;
        bool valid = false;
    };

    static constexpr uint8_t kMaximumVisibleTiles = 9;
    static constexpr size_t kPsramTileCacheSize = 24; // 24 tiles in PSRAM for route pre-fetching (~3.0 MB)

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

    // PSRAM Double Buffering Sprites
    lgfx::LGFX_Sprite _mapSprite;    // 400x400 map canvas in PSRAM
    lgfx::LGFX_Sprite _frameSprite;  // 320x240 full screen framebuffer in PSRAM
    bool _spritesInitialized = false;

    // Pure PSRAM Tile Cache (No flash writes)
    CachedTile _psramTileCache[kPsramTileCacheSize];
    uint8_t* _downloadBuffer = nullptr;

    portMUX_TYPE _taskMux = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t _downloadTask = nullptr;
    bool _jobPending = false;
    bool _jobActive = false;
    TileKey _jobTile;
    TileKey _activeTile;
    bool _resultPending = false;
    bool _resultOk = false;
    TileKey _resultTile;
    size_t _resultSize = 0;
    char _resultError[48]{};

    static bool sameTile(const TileKey& left, const TileKey& right);
    static void project(double latitude,
                        double longitude,
                        uint8_t zoom,
                        double& worldX,
                        double& worldY);

    void initPsramSprites();
    bool getCachedTileSprite(const TileKey& tile, lgfx::LGFX_Sprite*& outSprite);
    bool isTileInCache(const TileKey& tile) const;

    bool collectVisibleTiles(TileKey* tiles,
                             uint8_t& count,
                             TileKey& centerTile,
                             double& viewportWorldX,
                             double& viewportWorldY) const;
    bool renderMap();
    void drawRoute(double viewportWorldX, double viewportWorldY);
    void drawVehicleMarkerHeadingUp(int16_t x, int16_t y);
    void drawNavigationOverlay();
    void drawManeuverIcon(Maneuver maneuver, int16_t x, int16_t y);
    void drawWrappedRoadName(const String& road, int16_t x, int16_t y, int16_t width);
    void showFallback();
    void showGpsLostOverlay();

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
    bool downloadTile(const TileKey& tile, size_t& receivedBytes, String& error);
};

#endif
