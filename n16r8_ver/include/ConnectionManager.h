#pragma once

#include <Arduino.h>
#include <WebServer.h>

class DisplayManager;
class GpsManager;
class MediaManager;
class ModeController;
class RouteManager;
class SettingsStore;

class ConnectionManager {
public:
    ConnectionManager(DisplayManager* display,
                      MediaManager* media,
                      ModeController* modeController,
                      GpsManager* gps,
                      RouteManager* route,
                      SettingsStore* settings);

    void begin();
    void loop();
    bool internetConnected() const;
    String webAddress() const;

private:
    enum class NetworkState : uint8_t {
        Connecting,
        Station,
        AccessPoint,
    };

    WebServer _server{80};
    DisplayManager* _display;
    MediaManager* _media;
    ModeController* _modeController;
    GpsManager* _gps;
    RouteManager* _route;
    SettingsStore* _settings;
    NetworkState _networkState = NetworkState::Connecting;
    String _stationSsid;
    String _stationPassword;
    uint32_t _connectionStartedMs = 0;
    uint32_t _nextReconnectMs = 0;
    uint32_t _stationSwitchAtMs = 0;
    uint8_t _reconnectAttempt = 0;
    bool _mdnsStarted = false;
    bool _uploadRequestOk = false;
    String _uploadRequestError;
    String _testImageUploadData;
    String _testImageUploadError;
    bool _testImageUploadOk = false;

    void configureRoutes();
    void startStation();
    void startAccessPoint();
    void handleStationConnected();
    void scheduleStationSwitch(uint32_t delayMs);
    void updateNetwork();
    bool navigationActive() const;
    const char* networkStateName() const;

    void handleStatus();
    void handleConfig();
    void handleWifiConfig();
    void handleDestination();
    void handleNavigationStop();
    void handleNavigationReroute();
    void handleTestImage();
    void handleTestImageUpload();
    void handleTestImageClear();
    void handleAnimationBegin();
    void handleAnimationFrameUpload();
    void handleAnimationCommit();
    void handleAnimationDelete();

    void sendJson(const String& body, int status = 200);
    void sendError(const String& error, int status);
    bool serveFile(const char* path, const char* contentType);
};
