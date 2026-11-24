#pragma once

#if defined(MOTOMAP_NAV_MAP)

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <LovyanGFX.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
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
    String lastError() const;

    struct TileKey {
        int32_t x = 0;
        int32_t y = 0;
        uint8_t zoom = 0;

        TileKey() = default;
        TileKey(int32_t tileX, int32_t tileY, uint8_t tileZoom)
            : x(tileX), y(tileY), zoom(tileZoom) {}
    };

    bool saveOfflineTile(const TileKey& tile, const uint8_t* data, size_t length);
    static bool deleteOfflineTiles();
    static void getOfflineTileStats(size_t& count, size_t& totalBytes);

private:
    struct CachedTile {
        TileKey key;
        lgfx::LGFX_Sprite sprite;
        uint32_t lastAccess = 0;
        bool valid = false;
    };

    struct TileJob {
        TileKey tile;
        uint8_t bufferIndex = 0;
    };

    struct TileResult {
        bool ok = false;
        TileKey tile;
        uint8_t bufferIndex = 0;
        size_t size = 0;
        char error[48]{};
    };

    static constexpr uint8_t kMaximumVisibleTiles = 9;
    static constexpr size_t kPsramTileCacheSize = 24; // 24 tiles in PSRAM (~3.0 MB)

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

    // Pure PSRAM Tile Cache + Core 0/1 isolated buffers
    CachedTile _psramTileCache[kPsramTileCacheSize];
    uint8_t* _downloadBuffers[2]{nullptr, nullptr}; // Ping-pong buffers for Core 0 network download
    uint8_t* _flashTileBuffer = nullptr;            // Core 1 dedicated buffer for LittleFS reads
    uint8_t _nextBufferIndex = 0;

    TaskHandle_t _downloadTask = nullptr;
    QueueHandle_t _tileJobQueue = nullptr;
    QueueHandle_t _tileResultQueue = nullptr;
    bool _tileInFlight = false;
    TileKey _activeTile;

    static bool sameTile(const TileKey& left, const TileKey& right);
    static void project(double latitude,
                        double longitude,
                        uint8_t zoom,
                        double& worldX,
                        double& worldY);

    void initPsramSprites();
    bool getCachedTileSprite(const TileKey& tile, lgfx::LGFX_Sprite*& outSprite);
    bool loadTileFromFlash(const TileKey& tile, lgfx::LGFX_Sprite*& outSprite);
    bool isTileInCache(const TileKey& tile) const;
    static String tilePath(const TileKey& tile);

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
    bool downloadBusy() const;
    bool tileBusy(const TileKey& tile) const;
    bool enqueueTile(const TileKey& tile);
    void processDownloadResult();

    static void downloadTaskThunk(void* parameter);
    void downloadLoop();
    bool downloadTile(const TileKey& tile, uint8_t* buffer, size_t& receivedBytes, String& error);
    static bool writeOfflineTileFile(const TileKey& tile, const uint8_t* data, size_t length);
};

#endif
