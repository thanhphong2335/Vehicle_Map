#include "ConnectionManager.h"
#include "DebugLog.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <ctime>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>

#include "DisplayManager.h"
#include "GpsManager.h"
#include "MediaManager.h"
#include "ModeController.h"
#include "RouteManager.h"
#include "AlertManager.h"
#include "ImuManager.h"
#include "LedManager.h"
#include "SettingsStore.h"
#include "SyncGuards.h"
#if defined(MOTOMAP_NAV_MAP)
#include "NavigationMapManager.h"
#endif

namespace {
constexpr char kApSsid[] = "MotoMap-ESP32";
constexpr char kApPassword[] = "motomap123";
constexpr char kMdnsName[] = "motomap";
constexpr uint32_t kStationTimeoutMs = 20000;
constexpr uint32_t kReconnectDelaysMs[] = {5000, 15000, 30000};
constexpr uint32_t kWebTaskStack = 8192;
constexpr UBaseType_t kWebTaskPriority = 2;
constexpr BaseType_t kWebTaskCore = 0;

bool parseBody(WebServer& server, JsonDocument& document, String& error) {
    if (!server.hasArg("plain") || server.arg("plain").length() == 0) {
        error = "missing_json_body";
        return false;
    }
    const DeserializationError jsonError = deserializeJson(document, server.arg("plain"));
    if (jsonError) {
        error = "invalid_json";
        return false;
    }
    return true;
}
}

ConnectionManager::ConnectionManager(DisplayManager* display,
                                     MediaManager* media,
                                     ModeController* modeController,
                                     GpsManager* gps,
                                     RouteManager* route,
                                     SettingsStore* settings,
                                     NavigationMapManager* mapManager,
                                     AlertManager* alertManager,
                                     ImuManager* imu)
    : _display(display),
      _media(media),
      _modeController(modeController),
      _gps(gps),
      _route(route),
      _settings(settings),
      _mapManager(mapManager),
      _alertManager(alertManager),
      _imu(imu) {}

ConnectionManager::~ConnectionManager() {
    freeTestImageUploadBuffer();
    if (_webTaskHandle != nullptr) {
        vTaskDelete(_webTaskHandle);
        _webTaskHandle = nullptr;
    }
    if (_netMutex != nullptr) {
        vSemaphoreDelete(_netMutex);
        _netMutex = nullptr;
    }
}

void ConnectionManager::freeTestImageUploadBuffer() {
    if (_testImageUploadBuf != nullptr) {
        free(_testImageUploadBuf);
        _testImageUploadBuf = nullptr;
    }
    _testImageUploadLen = 0;
}

void ConnectionManager::remountFsIfNeeded() {
    if (!_otaFsUnmounted) return;
    FsLock fsLock(pdMS_TO_TICKS(2000));
    if (fsLock.locked() && LittleFS.begin(true)) {
        _otaFsUnmounted = false;
        Serial.println("[OTA] LittleFS remounted after aborted/failed filesystem OTA.");
    }
}

void ConnectionManager::begin() {
    if (_netMutex == nullptr) {
        _netMutex = xSemaphoreCreateRecursiveMutex();
    }
    {
        FsLock fsLock;
        if (!fsLock.locked() || !LittleFS.begin(true)) {
            Serial.println("LittleFS mount failed in ConnectionManager");
        }
    }
    configureRoutes();

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(true);
    if (_settings != nullptr && _settings->loadWifi(_stationSsid, _stationPassword)) {
        startStation();
    } else {
        startAccessPoint();
    }

    // Start the HTTP server only after WiFi has initialized the TCP/IP stack.
    _server.begin();

    // Setup ArduinoOTA for direct IDE/PlatformIO flashing over Wi-Fi
    ArduinoOTA.setHostname("motomap-esp32");
    ArduinoOTA.onStart([this]() {
        OtaGuard::setActive(true);
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        if (ArduinoOTA.getCommand() == U_SPIFFS) {
            FsLock fsLock;
            LittleFS.end();
            _otaFsUnmounted = true;
        }
        Serial.printf("[ArduinoOTA] Start updating %s\n", type.c_str());
        UiActionQueue::pushStatus("ARDUINO OTA", type.c_str(), TFT_MAGENTA);
        if (_led != nullptr) {
            _led->setPattern(LedPattern::PoliceStrobe, 0xFF00FF);
        }
    });
    ArduinoOTA.onEnd([this]() {
        _otaFsUnmounted = false;
        Serial.println("\n[ArduinoOTA] Update complete. Rebooting...");
        UiActionQueue::pushStatus("OTA SUCCESS", "Rebooting...", TFT_GREEN);
        if (_led != nullptr) {
            _led->setPattern(LedPattern::Solid, 0x00FF00);
        }
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        esp_task_wdt_reset();
        if (total > 0) {
            const unsigned int pct = static_cast<unsigned int>((static_cast<uint64_t>(progress) * 100ULL) / total);
            Serial.printf("[ArduinoOTA] Progress: %u%%\r", pct);
        }
    });
    ArduinoOTA.onError([this](ota_error_t error) {
        OtaGuard::setActive(false);
        remountFsIfNeeded();
        Serial.printf("[ArduinoOTA] Error[%u]\n", error);
        char errBuf[24];
        snprintf(errBuf, sizeof(errBuf), "Code: %u", static_cast<unsigned>(error));
        UiActionQueue::pushStatus("OTA ERROR", errBuf, TFT_RED);
    });
    ArduinoOTA.begin();

    if (_webTaskHandle == nullptr) {
        const BaseType_t created = xTaskCreatePinnedToCore(
            webTaskThunk,
            "WebOtaTask",
            kWebTaskStack,
            this,
            kWebTaskPriority,
            &_webTaskHandle,
            kWebTaskCore);
        if (created == pdPASS) {
            Serial.println("[NET] WebOtaTask started on Core 0.");
        } else {
            Serial.println("[NET] ERROR: Failed to spawn WebOtaTask!");
        }
    }
}

void ConnectionManager::webTaskThunk(void* parameter) {
    auto* self = static_cast<ConnectionManager*>(parameter);
    self->webTaskLoop();
}

void ConnectionManager::webTaskLoop() {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        if (_dnsStarted) {
            _dnsServer.processNextRequest();
        }
        _server.handleClient();
        ArduinoOTA.handle();
        updateNetwork();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ConnectionManager::startStation() {
    if (_stationSsid.length() == 0 &&
        (_settings == nullptr || !_settings->loadWifi(_stationSsid, _stationPassword))) {
        DEBUG_LOG("WIFI", "startStation: No SSID configured, falling back to AP mode");
        startAccessPoint();
        return;
    }

    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }
    DEBUG_LOG("WIFI", "startStation: Connecting to SSID='%s'...", _stationSsid.c_str());
    WiFi.disconnect(true, false);
    // Keep the setup portal available even after joining the user's router.
    // This lets a phone change Wi-Fi credentials at 192.168.4.1 without first
    // taking the old router offline.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApSsid, kApPassword);
    startDnsServer();
    WiFi.begin(_stationSsid.c_str(), _stationPassword.c_str());
    _networkState = NetworkState::Connecting;
    _connectionStartedMs = millis();
    _nextReconnectMs = millis() + kReconnectDelaysMs[0];
    _reconnectAttempt = 0;
    Serial.printf("Connecting to WiFi: %s\n", _stationSsid.c_str());
    DEBUG_LOG("WIFI", "WiFi.begin initiated. Timeout in %u ms.", kStationTimeoutMs);
    if (_display != nullptr && !navigationActive() && (_media == nullptr || !_media->hasTestImage())) {
        const String msg = String("Connecting ") + _stationSsid;
        UiActionQueue::pushIdle(msg.c_str());
    }
}

void ConnectionManager::startAccessPoint() {
    DEBUG_LOG("WIFI", "startAccessPoint: Falling back to AP mode (SSID='%s')...", kApSsid);
    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }

    const bool hasStation = _settings != nullptr &&
                            _settings->loadWifi(_stationSsid, _stationPassword);
    WiFi.disconnect(true, false);
    WiFi.mode(hasStation ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);
    startDnsServer();
    if (hasStation) {
        WiFi.begin(_stationSsid.c_str(), _stationPassword.c_str());
        _connectionStartedMs = millis();
        _nextReconnectMs = millis() + kReconnectDelaysMs[2];
    }
    _networkState = NetworkState::AccessPoint;
    _reconnectAttempt = 2;

    if (MDNS.begin(kMdnsName)) {
        MDNS.addService("http", "tcp", 80);
        _mdnsStarted = true;
    }
    Serial.printf("MotoMap setup AP: %s, web: http://%s/\n",
                  kApSsid,
                  WiFi.softAPIP().toString().c_str());
    DEBUG_LOG("WIFI", "AP active on IP %s", WiFi.softAPIP().toString().c_str());
    handleStationDisconnected();
}

void ConnectionManager::startDnsServer() {
    if (_dnsStarted) return;
    if (_dnsServer.start(53, "*", WiFi.softAPIP())) {
        _dnsStarted = true;
        Serial.printf("[DNS] Captive Portal DNS server started on %s:53\n",
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[DNS] WARNING: Failed to start Captive Portal DNS server!");
    }
}

void ConnectionManager::stopDnsServer() {
    if (!_dnsStarted) return;
    _dnsServer.stop();
    _dnsStarted = false;
    Serial.println("[DNS] Captive Portal DNS server stopped.");
}

void ConnectionManager::handleStationConnected() {
    // Keep SoftAP running concurrently (WIFI_AP_STA)
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApSsid, kApPassword);
    startDnsServer();

    // Trigger non-blocking SNTP time sync for TLS certificate verification & timestamps
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");

    _networkState = NetworkState::Station;
    _connectionStartedMs = millis();
    _reconnectAttempt = 0;
    if (_mdnsStarted) MDNS.end();
    _mdnsStarted = MDNS.begin(kMdnsName);
    if (_mdnsStarted) MDNS.addService("http", "tcp", 80);

    const IPAddress ip = WiFi.localIP();
    const IPAddress apIp = WiFi.softAPIP();
    _lastConnectedIp = ip;
    _lastConnectedSsid = WiFi.SSID();
    DEBUG_LOG("WIFI", ">>> STATION CONNECTED! SSID='%s', Station IP=%s, SoftAP IP=%s, RSSI=%d dBm",
              WiFi.SSID().c_str(), ip.toString().c_str(), apIp.toString().c_str(), WiFi.RSSI());
    Serial.printf("WiFi connected. Station IP: %s, SoftAP IP: %s, web: http://motomap.local/\n",
                  ip.toString().c_str(), apIp.toString().c_str());

    const bool navAct = navigationActive();
    const int routeState = _route ? static_cast<int>(_route->state()) : -1;
    const bool testImg = (_media != nullptr && _media->hasTestImage());
    const bool savedImg = (_media != nullptr && _media->hasSavedImage());

    DEBUG_LOG("DISPLAY", "Evaluating showIdle: _display=%p, navActive=%d (routeState=%d), testImage=%d, savedImage=%d",
              _display, navAct ? 1 : 0, routeState, testImg ? 1 : 0, savedImg ? 1 : 0);

    if (_display != nullptr) {
        const RouteState currentRs = _route != nullptr ? _route->state() : RouteState::Idle;
        const bool activelyGuiding = (currentRs == RouteState::Navigating || currentRs == RouteState::OffRoute);
        if (activelyGuiding) {
            DEBUG_LOG("DISPLAY", "Suppressed showIdle because active turn-by-turn driving guidance is in progress.");
        } else if (testImg) {
            DEBUG_LOG("DISPLAY", "Suppressed showIdle because test image is active.");
        } else {
            const String qrUrl = String("http://") + ip.toString() + "/";
            DEBUG_LOG("DISPLAY", "Queuing showIdle('%s')...", qrUrl.c_str());
            UiActionQueue::pushIdle(qrUrl.c_str());
        }
    }
}

void ConnectionManager::handleStationDisconnected() {
    _lastConnectedIp = IPAddress(0, 0, 0, 0);

    DEBUG_LOG("WIFI", "handleStationDisconnected: Reverting screen to standby/waiting mode...");

    const RouteState currentRs = _route != nullptr ? _route->state() : RouteState::Idle;
    const bool activelyGuiding = (currentRs == RouteState::Navigating || currentRs == RouteState::OffRoute);
    if (activelyGuiding) {
        DEBUG_LOG("DISPLAY", "Suppressed standby screen because active navigation is in progress.");
        return;
    }

    if (_media != nullptr && _media->hasTestImage()) {
        DEBUG_LOG("DISPLAY", "Suppressed standby screen because test image is active.");
        return;
    }

    if (_media != nullptr && _media->hasSavedImage()) {
        DEBUG_LOG("DISPLAY", "Station disconnected -> Restoring saved user image on Core 1.");
        UiAction action{};
        action.type = UiActionType::ShowSavedImage;
        UiActionQueue::push(action);
        return;
    }

    DEBUG_LOG("DISPLAY", "Station disconnected -> Queuing initial standby screen.");
    UiActionQueue::pushIdle("Waiting for iPhone");
}

void ConnectionManager::scheduleStationSwitch(uint32_t delayMs) {
    if (_settings == nullptr || !_settings->loadWifi(_stationSsid, _stationPassword)) {
        DEBUG_LOG("WIFI", "scheduleStationSwitch: Failed to load wifi from settings!");
        return;
    }
    _stationSwitchAtMs = millis() + delayMs;
    DEBUG_LOG("WIFI", "scheduleStationSwitch: Swapping to SSID='%s' in %u ms (at ms=%u)",
              _stationSsid.c_str(), delayMs, _stationSwitchAtMs);
}

void ConnectionManager::updateNetwork() {
    const uint32_t now = millis();
    if (_stationSwitchAtMs != 0 && static_cast<int32_t>(now - _stationSwitchAtMs) >= 0) {
        _stationSwitchAtMs = 0;
        DEBUG_LOG("WIFI", "Station switch timer expired. Calling startStation()...");
        startStation();
        return;
    }

    const wl_status_t wifiStatus = WiFi.status();
    static wl_status_t sLastWifiStatus = WL_IDLE_STATUS;
    if (wifiStatus != sLastWifiStatus) {
        DEBUG_LOG("WIFI", "Status change: %s (%d) -> %s (%d)",
                  DebugLog::wifiStatusName(sLastWifiStatus), static_cast<int>(sLastWifiStatus),
                  DebugLog::wifiStatusName(wifiStatus), static_cast<int>(wifiStatus));
        sLastWifiStatus = wifiStatus;
    }

    if (wifiStatus == WL_CONNECTED) {
        const IPAddress localIp = WiFi.localIP();
        if (localIp[0] == 0) {
            static uint32_t sLastDhcpWaitMs = 0;
            if (now - sLastDhcpWaitMs >= 1000) {
                sLastDhcpWaitMs = now;
                DEBUG_LOG("WIFI", "WL_CONNECTED established, waiting for DHCP to assign valid IP...");
            }
            return;
        }

        if (_networkState != NetworkState::Station || localIp != _lastConnectedIp || WiFi.SSID() != _lastConnectedSsid) {
            DEBUG_LOG("WIFI", "Station connected or network changed! Old IP=%s, New IP=%s, SSID='%s'",
                      _lastConnectedIp.toString().c_str(), localIp.toString().c_str(), WiFi.SSID().c_str());
            handleStationConnected();
        }
        return;
    }

    if (_networkState == NetworkState::Station) {
        DEBUG_LOG("WIFI", "Station disconnected! Entering Connecting state...");
        _networkState = NetworkState::Connecting;
        _connectionStartedMs = now;
        _nextReconnectMs = now;
        _reconnectAttempt = 0;
        handleStationDisconnected();
    } else if (_display != nullptr && _display->isShowingQr()) {
        static uint32_t sLastQrRevertMs = 0;
        if (now - sLastQrRevertMs >= 500) {
            sLastQrRevertMs = now;
            DEBUG_LOG("WIFI", "Wi-Fi is not connected but QR code is visible -> Reverting to standby!");
            handleStationDisconnected();
        }
    }

    if (_networkState == NetworkState::Connecting) {
        if (now - _connectionStartedMs >= kStationTimeoutMs) {
            DEBUG_LOG("WIFI", "Connection timed out after %u ms! Fallback to AP.", kStationTimeoutMs);
            startAccessPoint();
            return;
        }
        if (static_cast<int32_t>(now - _nextReconnectMs) >= 0) {
            DEBUG_LOG("WIFI", "Reconnecting to WiFi (attempt %u)...", _reconnectAttempt);
            WiFi.reconnect();
            const uint8_t delayIndex = _reconnectAttempt > 2 ? 2 : _reconnectAttempt;
            _nextReconnectMs = now + kReconnectDelaysMs[delayIndex];
            if (_reconnectAttempt < 2) ++_reconnectAttempt;
        }
        return;
    }

    if (_networkState == NetworkState::AccessPoint &&
        _stationSsid.length() > 0 &&
        static_cast<int32_t>(now - _nextReconnectMs) >= 0) {
        WiFi.reconnect();
        _nextReconnectMs = now + kReconnectDelaysMs[2];
    }
}

bool ConnectionManager::internetConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String ConnectionManager::webAddress() const {
    if (internetConnected()) return String("http://") + WiFi.localIP().toString() + '/';
    return String("http://") + WiFi.softAPIP().toString() + '/';
}

bool ConnectionManager::navigationActive() const {
    if (_route == nullptr) return false;
    const RouteState state = _route->state();
    return state == RouteState::WaitingGps ||
           state == RouteState::Requesting ||
           state == RouteState::Navigating ||
           state == RouteState::OffRoute ||
           state == RouteState::Error;
}

const char* ConnectionManager::networkStateName() const {
    switch (_networkState) {
        case NetworkState::Connecting: return "CONNECTING";
        case NetworkState::Station: return "STATION";
        case NetworkState::AccessPoint: return "ACCESS_POINT";
    }
    return "UNKNOWN";
}

void ConnectionManager::configureRoutes() {
    _server.on("/", HTTP_GET, [this]() { serveFile("/index.html", "text/html; charset=utf-8"); });
    _server.on("/index.html", HTTP_GET, [this]() { serveFile("/index.html", "text/html; charset=utf-8"); });
    _server.on("/env.js", HTTP_GET, [this]() {
        bool envExists = false;
        {
            FsLock fsLock(pdMS_TO_TICKS(500));
            envExists = fsLock.locked() && LittleFS.exists("/env.js");
        }
        if (!envExists) {
            _server.send(200, "application/javascript; charset=utf-8", "window.MOTOMAP_ENV={};");
            return;
        }
        serveFile("/env.js", "application/javascript; charset=utf-8");
    });
    _server.on("/app.js", HTTP_GET, [this]() { serveFile("/app.js", "application/javascript; charset=utf-8"); });
    _server.on("/styles.css", HTTP_GET, [this]() { serveFile("/styles.css", "text/css; charset=utf-8"); });
    _server.on("/apple-touch-icon.png", HTTP_GET, [this]() { serveFile("/apple-touch-icon.png", "image/png"); });

    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/config", HTTP_GET, [this]() { handleConfig(); });
    _server.on("/api/config/wifi", HTTP_POST, [this]() { handleWifiConfig(); });
    _server.on("/api/destination", HTTP_POST, [this]() { handleDestination(); });
    _server.on("/api/navigation/stop", HTTP_POST, [this]() { handleNavigationStop(); });
    _server.on("/navigation/stop", HTTP_POST, [this]() { handleNavigationStop(); });
    _server.on("/api/navigation/reroute", HTTP_POST, [this]() { handleNavigationReroute(); });
    _server.on("/api/alert-config", HTTP_GET, [this]() { handleAlertConfigGet(); });
    _server.on("/api/alert-config", HTTP_POST, [this]() { handleAlertConfigPost(); });
    _server.on("/api/alert-test", HTTP_POST, [this]() { handleAlertTest(); });
    _server.on("/api/imu", HTTP_GET, [this]() { handleImuApi(); });
    _server.on("/api/imu/clear-alert", HTTP_POST, [this]() { handleImuClearAlert(); });
    _server.on("/api/led", HTTP_GET, [this]() { handleLedApiGet(); });
    _server.on("/api/led", HTTP_POST, [this]() { handleLedApiPost(); });
    _server.on("/api/ota/info", HTTP_GET, [this]() { handleOtaInfo(); });
    _server.on(
        "/api/ota/update",
        HTTP_POST,
        [this]() { handleOtaUpdateDone(); },
        [this]() { handleOtaUploadProgress(); });

    _server.on("/mode/media", HTTP_POST, [this]() {
        if (navigationActive()) {
            sendError("navigation_has_priority", 409);
            return;
        }
        UiAction action{};
        action.type = UiActionType::EnterMediaMode;
        action.flag = true;
        UiActionQueue::push(action);
        sendJson("{\"ok\":true,\"mode\":\"MEDIA\"}");
    });
    _server.on("/mode/idle", HTTP_POST, [this]() {
        if (_media != nullptr) _media->clearTestImage();
        UiActionQueue::pushIdle("Waiting for iPhone");
        sendJson("{\"ok\":true,\"mode\":\"IDLE\"}");
    });
    _server.on("/media/frame", HTTP_POST, [this]() {
        if (navigationActive()) {
            sendError("navigation_has_priority", 409);
            return;
        }
        if (!_server.hasArg("plain")) {
            sendError("missing_jpeg", 400);
            return;
        }
        const String& body = _server.arg("plain");
        const bool ok = _media != nullptr && _media->queueStreamJpeg(
            reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
        if (ok) sendJson("{\"ok\":true}");
        else sendError("invalid_jpeg", 400);
    });
    _server.on(
        "/api/media/test-image",
        HTTP_POST,
        [this]() { handleTestImage(); },
        [this]() { handleTestImageUpload(); });
    _server.on("/api/media/test-image", HTTP_DELETE, [this]() { handleTestImageClear(); });

    _server.on("/api/media/animation/begin", HTTP_POST, [this]() { handleAnimationBegin(); });
    _server.on(
        "/api/media/animation/frame",
        HTTP_POST,
        [this]() {
            if (_uploadRequestOk) sendJson("{\"ok\":true}");
            else sendError(_uploadRequestError.length() ? _uploadRequestError : "frame_upload_failed", 400);
        },
        [this]() { handleAnimationFrameUpload(); });
    _server.on("/api/media/animation/commit", HTTP_POST, [this]() { handleAnimationCommit(); });
    _server.on("/api/media/animation", HTTP_DELETE, [this]() { handleAnimationDelete(); });

    _server.on("/api/map/offline/stats", HTTP_GET, [this]() { handleMapOfflineStats(); });
    _server.on("/api/map/offline", HTTP_DELETE, [this]() { handleMapOfflineDelete(); });
    _server.on(
        "/api/map/tile",
        HTTP_POST,
        [this]() {
            if (_uploadRequestOk) sendJson("{\"ok\":true}");
            else sendError(_uploadRequestError.length() ? _uploadRequestError : "tile_upload_failed", 400);
        },
        [this]() { handleMapTileUpload(); });

    _server.on("/api/debug", HTTP_GET, [this]() {
        _server.send(200, "text/plain; charset=utf-8", DebugLog::getLogs());
    });
    _server.on("/api/debug/clear", HTTP_POST, [this]() {
        DebugLog::clearLogs();
        _server.send(200, "application/json", "{\"ok\":true,\"cleared\":true}");
    });
    _server.on("/debug", HTTP_GET, [this]() {
        _server.send(200, "text/plain; charset=utf-8", DebugLog::getLogs());
    });

    // Captive portal detection endpoints for Apple iOS, Android, and Windows
    auto handleCaptiveRedirect = [this]() {
        const String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        _server.sendHeader("Location", portalUrl, true);
        _server.send(302, "text/plain", "");
    };

    _server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect); // Apple iOS/macOS CNA
    _server.on("/canonical.html", HTTP_GET, handleCaptiveRedirect);
    _server.on("/success.txt", HTTP_GET, handleCaptiveRedirect);
    _server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);        // Android NetworkMonitor
    _server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);
    _server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);            // Windows
    _server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
    _server.on("/redirect", HTTP_GET, handleCaptiveRedirect);

    _server.onNotFound([this]() {
        if (_server.method() == HTTP_OPTIONS) {
            _server.sendHeader("Access-Control-Allow-Origin", "*");
            _server.sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
            _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
            _server.send(204);
            return;
        }
        const String host = _server.hostHeader();
        if (host.startsWith("192.168.4.") || host.indexOf("motomap") != -1 || _networkState != NetworkState::Station) {
            const String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
            _server.sendHeader("Location", portalUrl, true);
            _server.send(302, "text/plain", "");
            return;
        }
        sendError("not_found", 404);
    });
}

void ConnectionManager::handleStatus() {
    size_t fsTotal = 0;
    size_t fsUsed = 0;
    {
        FsLock fsLock(pdMS_TO_TICKS(500));
        if (fsLock.locked()) {
            fsTotal = LittleFS.totalBytes();
            fsUsed = LittleFS.usedBytes();
        }
    }

    JsonDocument document;
    document["device"] = "MotoMap-ESP32";
    document["mode"] = _modeController != nullptr ? modeName(_modeController->mode()) : "UNKNOWN";
    RouteManager::StatusSnapshot routeSnap;
    if (_route != nullptr) {
        _route->getStatusSnapshot(routeSnap);
    }
    document["route_state"] = _route != nullptr ? routeStateName(routeSnap.state) : "UNKNOWN";
    document["network_state"] = networkStateName();
    document["internet_connected"] = internetConnected();
    document["ip"] = internetConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    document["web_url"] = webAddress();
    document["rssi"] = internetConnected() ? WiFi.RSSI() : 0;
    document["uptime_ms"] = millis();
    document["heap_free"] = ESP.getFreeHeap();
    document["littlefs_total"] = fsTotal;
    document["littlefs_used"] = fsUsed;
    document["display_width"] = _display != nullptr ? _display->width() : 0;
    document["display_height"] = _display != nullptr ? _display->height() : 0;

    if (_gps != nullptr) {
        const GpsSnapshot gps = _gps->snapshot();
        JsonObject jsonGps = document["gps"].to<JsonObject>();
        jsonGps["valid"] = gps.valid;
        jsonGps["lat"] = gps.latitude;
        jsonGps["lon"] = gps.longitude;
        jsonGps["speed_kmh"] = gps.speedKmh;
        jsonGps["course_deg"] = gps.courseDeg;
        jsonGps["satellites"] = gps.satellites;
        jsonGps["hdop"] = gps.hdop;
        jsonGps["age_ms"] = gps.ageMs;
        jsonGps["characters"] = _gps->charactersProcessed();
        jsonGps["source"] = gps.valid ? "NEO_6M" : "WAITING";
    }
    if (_route != nullptr) {
        const DestinationData& destination = routeSnap.destination;
        JsonObject jsonDestination = document["destination"].to<JsonObject>();
        jsonDestination["valid"] = destination.valid;
        jsonDestination["active"] = destination.active;
        jsonDestination["lat"] = destination.latitude;
        jsonDestination["lon"] = destination.longitude;
        jsonDestination["label"] = destination.label;
        jsonDestination["vehicle"] = destination.vehicle;
        document["navigation_sequence"] = routeSnap.sequence;
        document["navigation_age_ms"] = routeSnap.lastUpdateMs == 0
            ? 0
            : millis() - routeSnap.lastUpdateMs;
        document["route_error"] = routeSnap.lastError;
        const NavigationData& navigation = routeSnap.navigation;
        JsonObject jsonNavigation = document["navigation"].to<JsonObject>();
        jsonNavigation["maneuver"] = maneuverName(navigation.maneuver);
        jsonNavigation["distance_to_turn_m"] = navigation.distanceToTurnM;
        jsonNavigation["remaining_distance_m"] = navigation.remainingDistanceM;
        jsonNavigation["eta_seconds"] = navigation.etaSeconds;
        jsonNavigation["road_name"] = navigation.roadName;
        jsonNavigation["status"] = navigation.statusMessage;
    }
    if (_media != nullptr) {
        const MediaManager::MediaStats mStats = _media->statsSnapshot();
        JsonObject animation = document["animation"].to<JsonObject>();
        animation["valid"] = mStats.animationValid;
        animation["frames"] = mStats.animationFrames;
        animation["fps"] = mStats.animationFps;
        animation["bytes"] = mStats.animationBytes;
        document["media_frames"] = mStats.streamFrameCount;
        document["test_image_active"] = mStats.testImageActive;
    }

    String body;
    serializeJson(document, body);
    sendJson(body);
}

void ConnectionManager::handleConfig() {
    JsonDocument document;
    String ssid;
    String ignoredPassword;
    const bool configured = _settings != nullptr && _settings->loadWifi(ssid, ignoredPassword);
    document["wifi_configured"] = configured;
    document["wifi_ssid"] = configured ? ssid : "";
    document["ap_ssid"] = kApSsid;
    document["ap_ip"] = WiFi.softAPIP().toString();
    document["station_ip"] = internetConnected() ? WiFi.localIP().toString() : "";
    String body;
    serializeJson(document, body);
    sendJson(body);
}

void ConnectionManager::handleWifiConfig() {
    JsonDocument document;
    String error;
    if (!parseBody(_server, document, error)) {
        DEBUG_LOG("WIFI", "handleWifiConfig JSON parse error: %s", error.c_str());
        sendError(error, 400);
        return;
    }
    const String ssid = String(document["ssid"] | "");
    const String password = String(document["password"] | "");
    const bool connectNow = document["connect_now"] | false;
    DEBUG_LOG("WIFI", "Received /api/config/wifi: SSID='%s', passLen=%u, connect_now=%d",
              ssid.c_str(), password.length(), connectNow ? 1 : 0);
    if (_settings == nullptr || !_settings->saveWifi(ssid, password)) {
        DEBUG_LOG("WIFI", "ERROR: Failed to save wifi configuration to SettingsStore!");
        sendError("invalid_wifi_config", 400);
        return;
    }
    _stationSsid = ssid;
    _stationPassword = password;
    if (connectNow || (WiFi.status() == WL_CONNECTED && WiFi.SSID() != ssid)) {
        DEBUG_LOG("WIFI", "Scheduling station switch in 5000 ms...");
        scheduleStationSwitch(5000);
    }
    sendJson(connectNow
        ? "{\"ok\":true,\"switching_to_hotspot_in_ms\":5000}"
        : "{\"ok\":true,\"saved\":true}");
}

void ConnectionManager::handleDestination() {
    JsonDocument document;
    String error;
    if (!parseBody(_server, document, error)) {
        sendError(error, 400);
        return;
    }
    DestinationData destination;
    destination.latitude = document["lat"] | 1000.0;
    destination.longitude = document["lon"] | 1000.0;
    destination.label = String(document["label"] | "Pinned destination");
    destination.vehicle = String(document["vehicle"] | "motorcycle");
    destination.active = document["start"] | true;
    destination.valid = destination.latitude >= -90.0 && destination.latitude <= 90.0 &&
                        destination.longitude >= -180.0 && destination.longitude <= 180.0;
    if (_route == nullptr || !_route->setDestination(destination, &error)) {
        sendError(error.length() ? error : "destination_failed", 400);
        return;
    }
    if (!internetConnected() && _settings != nullptr && _settings->hasWifi()) {
        scheduleStationSwitch(5000);
    }
    sendJson(internetConnected()
        ? "{\"ok\":true,\"navigation\":\"waiting_gps\"}"
        : "{\"ok\":true,\"navigation\":\"waiting_hotspot\",\"switching_to_hotspot_in_ms\":5000}");
}

void ConnectionManager::handleNavigationStop() {
    if (_route == nullptr) {
        sendError("navigation_unavailable", 503);
        return;
    }
    _route->stop();
    sendJson("{\"ok\":true,\"mode\":\"IDLE\"}");
}

void ConnectionManager::handleNavigationReroute() {
    if (_route == nullptr || !_route->destination().active) {
        sendError("no_active_destination", 409);
        return;
    }
    _route->requestReroute();
    sendJson("{\"ok\":true,\"route_state\":\"OFF_ROUTE\"}");
}

void ConnectionManager::handleTestImage() {
    if (navigationActive()) {
        sendError("navigation_has_priority", 409);
        return;
    }
    if (!_testImageUploadOk) {
        sendError(_testImageUploadError.length() ? _testImageUploadError : "image_upload_failed", 400);
        return;
    }

    UiAction action{};
    action.type = UiActionType::EnterMediaMode;
    action.flag = false;
    UiActionQueue::push(action);
    sendJson("{\"ok\":true,\"display\":\"test_image\"}");
}

void ConnectionManager::handleTestImageUpload() {
    constexpr size_t kMaxUploadBytes = 128U * 1024U;
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        freeTestImageUploadBuffer();
        _testImageUploadOk = !navigationActive();
        _testImageUploadError = _testImageUploadOk ? "" : "navigation_has_priority";
        if (_testImageUploadOk) {
            _testImageUploadBuf = static_cast<uint8_t*>(ps_malloc(kMaxUploadBytes));
            if (_testImageUploadBuf == nullptr) {
                _testImageUploadBuf = static_cast<uint8_t*>(malloc(kMaxUploadBytes));
            }
            if (_testImageUploadBuf == nullptr) {
                _testImageUploadOk = false;
                _testImageUploadError = "image_oom";
            }
        }
        return;
    }
    if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        if (!_testImageUploadOk || _testImageUploadBuf == nullptr) return;
        if (_testImageUploadLen + upload.currentSize > kMaxUploadBytes) {
            _testImageUploadOk = false;
            _testImageUploadError = "jpeg_too_large";
            return;
        }
        memcpy(_testImageUploadBuf + _testImageUploadLen, upload.buf, upload.currentSize);
        _testImageUploadLen += upload.currentSize;
        return;
    }
    if (upload.status == UPLOAD_FILE_END) {
        if (_testImageUploadOk && _testImageUploadBuf != nullptr) {
            _testImageUploadOk = _media != nullptr && _media->saveTestImageOnly(
                _testImageUploadBuf, _testImageUploadLen);
            if (!_testImageUploadOk && !_testImageUploadError.length()) {
                _testImageUploadError = "jpeg_decode_failed";
            }
        }
        freeTestImageUploadBuffer();
        return;
    }
    if (upload.status == UPLOAD_FILE_ABORTED) {
        _testImageUploadOk = false;
        _testImageUploadError = "image_upload_aborted";
        freeTestImageUploadBuffer();
    }
}

void ConnectionManager::handleTestImageClear() {
    if (_media != nullptr) _media->clearTestImage();
    UiActionQueue::pushIdle("Image test stopped");
    sendJson("{\"ok\":true}");
}

void ConnectionManager::handleAnimationBegin() {
    if (navigationActive()) {
        sendError("navigation_has_priority", 409);
        return;
    }
    JsonDocument document;
    String error;
    if (!parseBody(_server, document, error)) {
        sendError(error, 400);
        return;
    }
    const uint16_t frames = document["frames"] | 0;
    const uint8_t fps = document["fps"] | 0;
    String uploadId;
    if (_media == nullptr || !_media->beginAnimationUpload(frames, fps, uploadId, error)) {
        sendError(error.length() ? error : "animation_begin_failed", 400);
        return;
    }
    JsonDocument response;
    response["ok"] = true;
    response["upload_id"] = uploadId;
    String body;
    serializeJson(response, body);
    sendJson(body);
}

void ConnectionManager::handleAnimationFrameUpload() {
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        _uploadRequestOk = !navigationActive();
        _uploadRequestError = _uploadRequestOk ? "" : "navigation_has_priority";
        if (_uploadRequestOk) {
            const String uploadId = _server.arg("upload_id");
            const uint16_t index = static_cast<uint16_t>(_server.arg("index").toInt());
            _uploadRequestOk = _media != nullptr &&
                               _media->beginAnimationFrame(uploadId, index, _uploadRequestError);
        }
        return;
    }
    if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        if (_uploadRequestOk) {
            _uploadRequestOk = _media->writeAnimationFrameChunk(
                upload.buf, upload.currentSize, _uploadRequestError);
        }
        return;
    }
    if (upload.status == UPLOAD_FILE_END) {
        if (_uploadRequestOk) {
            _uploadRequestOk = _media->finishAnimationFrame(_uploadRequestError);
        } else if (_media != nullptr) {
            _media->abortAnimationFrame();
        }
        return;
    }
    if (upload.status == UPLOAD_FILE_ABORTED) {
        _uploadRequestOk = false;
        _uploadRequestError = "frame_upload_aborted";
        if (_media != nullptr) _media->abortAnimationFrame();
    }
}

void ConnectionManager::handleAnimationCommit() {
    JsonDocument document;
    String error;
    if (!parseBody(_server, document, error)) {
        sendError(error, 400);
        return;
    }
    const String uploadId = String(document["upload_id"] | "");
    if (_media == nullptr || !_media->commitAnimationUpload(uploadId, error)) {
        sendError(error.length() ? error : "animation_commit_failed", 400);
        return;
    }
    sendJson("{\"ok\":true,\"animation\":\"ready\"}");
}

void ConnectionManager::handleAnimationDelete() {
    String error;
    if (_media == nullptr || !_media->clearAnimation(&error)) {
        sendError(error.length() ? error : "animation_delete_failed", 500);
        return;
    }
    UiActionQueue::pushIdle("Animation deleted");
    sendJson("{\"ok\":true}");
}

void ConnectionManager::sendJson(const String& body, int status) {
    _server.sendHeader("Cache-Control", "no-store");
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    _server.send(status, "application/json; charset=utf-8", body);
}

void ConnectionManager::sendError(const String& error, int status) {
    JsonDocument document;
    document["ok"] = false;
    document["error"] = error;
    String body;
    serializeJson(document, body);
    sendJson(body, status);
}

bool ConnectionManager::serveFile(const char* path, const char* contentType) {
    FsLock fsLock(pdMS_TO_TICKS(2000));
    if (!fsLock.locked()) {
        sendError("fs_busy", 503);
        return false;
    }
    if (!LittleFS.exists(path)) {
        sendError("file_not_found", 404);
        return false;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        sendError("file_open_failed", 500);
        return false;
    }
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");
    _server.streamFile(file, contentType);
    file.close();
    return true;
}

void ConnectionManager::loop() {
    // WebServer, Captive DNS, ArduinoOTA, and Wi-Fi state machine now run in Core 0 WebOtaTask.
}

void ConnectionManager::handleMapOfflineStats() {
    size_t count = 0;
    size_t totalBytes = 0;
#if defined(MOTOMAP_NAV_MAP)
    NavigationMapManager::getOfflineTileStats(count, totalBytes);
#endif
    size_t fsTotal = 0;
    size_t fsUsed = 0;
    {
        FsLock fsLock(pdMS_TO_TICKS(1000));
        if (fsLock.locked()) {
            fsTotal = LittleFS.totalBytes();
            fsUsed = LittleFS.usedBytes();
        }
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["tiles_count"] = count;
    doc["total_bytes"] = totalBytes;
    doc["total_kb"] = totalBytes / 1024;
    doc["littlefs_total"] = fsTotal;
    doc["littlefs_used"] = fsUsed;
    doc["littlefs_free"] = fsTotal >= fsUsed ? (fsTotal - fsUsed) : 0;
    String body;
    serializeJson(doc, body);
    sendJson(body);
}

void ConnectionManager::handleMapOfflineDelete() {
#if defined(MOTOMAP_NAV_MAP)
    NavigationMapManager::deleteOfflineTiles();
#endif
    sendJson("{\"ok\":true,\"message\":\"offline_tiles_cleared\"}");
}

void ConnectionManager::handleMapTileUpload() {
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        _uploadRequestOk = true;
        _uploadRequestError = "";
        const int32_t z = _server.arg("z").toInt();
        const int32_t x = _server.arg("x").toInt();
        const int32_t y = _server.arg("y").toInt();

        if (!_server.hasArg("z") || !_server.hasArg("x") || !_server.hasArg("y") ||
            z < 1 || z > 19 || x < 0 || y < 0) {
            _uploadRequestOk = false;
            _uploadRequestError = "invalid_tile_coordinates";
            return;
        }

        FsLock fsLock(pdMS_TO_TICKS(1500));
        if (!fsLock.locked()) {
            _uploadRequestOk = false;
            _uploadRequestError = "fs_busy";
            return;
        }
        if (!LittleFS.exists("/tiles")) {
            LittleFS.mkdir("/tiles");
        }

        char path[32];
        snprintf(path, sizeof(path), "/tiles/%ld_%ld_%ld.png",
                 static_cast<long>(z), static_cast<long>(x), static_cast<long>(y));
        if (_tileUploadFile) _tileUploadFile.close();
        _tileUploadFile = LittleFS.open(path, "w");
        if (!_tileUploadFile) {
            _uploadRequestOk = false;
            _uploadRequestError = "tile_open_failed";
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        if (_uploadRequestOk && _tileUploadFile) {
            FsLock fsLock(pdMS_TO_TICKS(1500));
            if (!fsLock.locked() || _tileUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
                _uploadRequestOk = false;
                _uploadRequestError = fsLock.locked() ? "tile_write_failed" : "fs_busy";
            }
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        FsLock fsLock(pdMS_TO_TICKS(1500));
        if (_tileUploadFile) {
            _tileUploadFile.close();
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        _uploadRequestOk = false;
        _uploadRequestError = "tile_upload_aborted";
        FsLock fsLock(pdMS_TO_TICKS(1500));
        if (_tileUploadFile) {
            _tileUploadFile.close();
        }
    }
}



void ConnectionManager::handleAlertConfigGet() {
    JsonDocument doc;
    if (_settings != nullptr) {
        AlertConfig config;
        _settings->loadAlertConfig(config);
        doc["enabled"] = config.enabled;
        doc["gateway_url"] = config.gatewayUrl;
        doc["chat_ids"] = config.chatIds;
        if (config.authKey.length() > 4) {
            String masked = "";
            for (size_t i = 0; i < config.authKey.length() - 4; ++i) masked += '*';
            masked += config.authKey.substring(config.authKey.length() - 4);
            doc["auth_key"] = masked;
        } else if (!config.authKey.isEmpty()) {
            doc["auth_key"] = "****";
        } else {
            doc["auth_key"] = "";
        }
    }
    if (_alertManager != nullptr) {
        doc["busy"] = _alertManager->isBusy();
        doc["last_status"] = _alertManager->lastStatus();
    }
    String body;
    serializeJson(doc, body);
    sendJson(body);
}

void ConnectionManager::handleAlertConfigPost() {
    JsonDocument doc;
    String error;
    if (!parseBody(_server, doc, error)) {
        sendError(error, 400);
        return;
    }
    if (_settings == nullptr) {
        sendError("settings_store_null", 500);
        return;
    }

    AlertConfig config;
    _settings->loadAlertConfig(config);

    if (!doc["enabled"].isNull()) {
        config.enabled = doc["enabled"] | true;
    }
    if (!doc["gateway_url"].isNull()) {
        config.gatewayUrl = String(doc["gateway_url"] | "");
    }
    if (!doc["chat_ids"].isNull()) {
        config.chatIds = String(doc["chat_ids"] | "");
    }
    if (!doc["auth_key"].isNull()) {
        String key = String(doc["auth_key"] | "");
        if (!key.startsWith("****") && !key.isEmpty()) {
            config.authKey = key;
        }
    }

    if (_settings->saveAlertConfig(config)) {
        sendJson("{\"ok\":true,\"saved\":true}");
    } else {
        sendError("failed_to_save_alert_config", 500);
    }
}

void ConnectionManager::handleAlertTest() {
    if (_alertManager == nullptr) {
        sendError("alert_manager_null", 500);
        return;
    }
    GpsSnapshot gps;
    if (_gps != nullptr) {
        gps = _gps->snapshot();
    }
    const bool queued = _alertManager->triggerTestAlert(gps);
    if (queued) {
        sendJson("{\"ok\":true,\"queued\":true}");
    } else {
        sendError("failed_to_queue_alert", 400);
    }
}


void ConnectionManager::handleImuApi() {
    if (_imu == nullptr) {
        sendError("imu_manager_null", 503);
        return;
    }
    ImuData data{};
    const bool ok = _imu->getData(data);
    if (!ok) {
        sendError("imu_data_unavailable", 503);
        return;
    }

    JsonDocument doc;
    doc["valid"] = data.valid;
    doc["connected"] = _imu->isSensorConnected();
    doc["ax"] = data.ax;
    doc["ay"] = data.ay;
    doc["az"] = data.az;
    doc["gx"] = data.gx;
    doc["gy"] = data.gy;
    doc["gz"] = data.gz;
    doc["temperature"] = data.temperature;
    doc["roll"] = data.roll;
    doc["pitch"] = data.pitch;
    doc["fall_detected"] = data.fallDetected;
    doc["fall_duration_ms"] = data.fallDurationMs;

    String body;
    serializeJson(doc, body);
    sendJson(body);
}

void ConnectionManager::handleImuClearAlert() {
    if (_imu != nullptr) {
        _imu->clearFallAlert();
    }
    sendJson("{\"ok\":true,\"cleared\":true}");
}

void ConnectionManager::handleLedApiGet() {
    JsonDocument doc;
    if (_led != nullptr) {
        doc["ok"] = true;
        doc["pattern"] = LedManager::patternName(_led->userPattern());
        doc["active_pattern"] = LedManager::patternName(_led->pattern());
        doc["brightness"] = _led->brightness();
    } else {
        doc["ok"] = false;
        doc["error"] = "led_not_available";
    }
    String body;
    serializeJson(doc, body);
    sendJson(body);
}

void ConnectionManager::handleLedApiPost() {
    if (_led == nullptr) {
        sendError("led_not_available", 503);
        return;
    }
    if (!_server.hasArg("plain")) {
        sendError("missing_body", 400);
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server.arg("plain"));
    if (error) {
        sendError("invalid_json", 400);
        return;
    }
    if (!doc["pattern"].isNull()) {
        const String patternStr = doc["pattern"].as<String>();
        const LedPattern pat = LedManager::patternFromName(patternStr);
        _led->setUserPattern(pat);
    }
    if (!doc["brightness"].isNull()) {
        int b = doc["brightness"].as<int>();
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        _led->setBrightness(static_cast<uint8_t>(b));
    }
    handleLedApiGet();
}

void ConnectionManager::handleOtaInfo() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);

    size_t fsTotal = 0;
    size_t fsUsed = 0;
    {
        FsLock fsLock(pdMS_TO_TICKS(500));
        if (fsLock.locked()) {
            fsTotal = LittleFS.totalBytes();
            fsUsed = LittleFS.usedBytes();
        }
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["running_partition"] = running ? running->label : "unknown";
    doc["next_partition"] = next ? next->label : "unknown";
    doc["flash_size_bytes"] = ESP.getFlashChipSize();
    doc["sketch_size_bytes"] = ESP.getSketchSize();
    doc["free_sketch_space"] = ESP.getFreeSketchSpace();
    doc["chip_model"] = ESP.getChipModel();
    doc["chip_revision"] = ESP.getChipRevision();
    doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["internal_free_heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    doc["free_psram"] = ESP.getFreePsram();
    doc["littlefs_total"] = fsTotal;
    doc["littlefs_used"] = fsUsed;
    doc["sntp_synced"] = (std::time(nullptr) >= 1700000000);

    String response;
    serializeJson(doc, response);
    sendJson(response);
}

void ConnectionManager::handleOtaUpdateDone() {
    _server.sendHeader("Connection", "close");
    if (Update.hasError()) {
        OtaGuard::setActive(false);
        remountFsIfNeeded();
        String err = String("OTA Update failed: ") + Update.errorString();
        Serial.printf("[OTA] %s\n", err.c_str());
        UiActionQueue::pushStatus("OTA ERROR", Update.errorString(), TFT_RED);
        if (_led != nullptr) {
            _led->setPattern(LedPattern::PoliceStrobe, 0xFF0000);
        }
        sendError(err, 500);
    } else {
        Serial.println("[OTA] Update completed successfully! Rebooting in 1s...");
        UiActionQueue::pushStatus("UPDATE SUCCESS", "Rebooting...", TFT_GREEN);
        if (_led != nullptr) {
            _led->setPattern(LedPattern::Solid, 0x00FF00);
        }
        sendJson("{\"ok\":true,\"message\":\"Update successful! Rebooting...\"}");
        _server.client().flush();
        delay(1000);
        ESP.restart();
    }
}

void ConnectionManager::handleOtaUploadProgress() {
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        OtaGuard::setActive(true);
        _otaInProgress = true;
        _otaWrittenBytes = 0;
        _otaError = "";

        // Determine if uploading firmware (U_FLASH) or filesystem (U_SPIFFS)
        int cmd = U_FLASH;
        if (_server.hasArg("type") && _server.arg("type") == "filesystem") {
            cmd = U_SPIFFS;
        } else if (upload.filename.indexOf("littlefs") >= 0 || upload.filename.indexOf("spiffs") >= 0) {
            cmd = U_SPIFFS;
        }
        _otaCommand = cmd;

        const char* typeName = (cmd == U_SPIFFS) ? "Filesystem" : "Firmware";
        Serial.printf("[OTA] Starting %s update. Filename: %s\n", typeName, upload.filename.c_str());

        if (cmd == U_SPIFFS) {
            FsLock fsLock;
            LittleFS.end();
            _otaFsUnmounted = true;
        }

        char detail[48];
        snprintf(detail, sizeof(detail), "Flashing %s", typeName);
        UiActionQueue::pushStatus("OTA UPDATING", detail, TFT_MAGENTA);
        if (_led != nullptr) {
            _led->setPattern(LedPattern::PoliceStrobe, 0xFF00FF);
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
            Update.printError(Serial);
            _otaError = Update.errorString();
            OtaGuard::setActive(false);
            remountFsIfNeeded();
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        _otaWrittenBytes += upload.currentSize;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
            _otaError = Update.errorString();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            _otaFsUnmounted = false;
            Serial.printf("[OTA] Update Success: %u bytes\n", _otaWrittenBytes);
        } else {
            Update.printError(Serial);
            _otaError = Update.errorString();
            OtaGuard::setActive(false);
            remountFsIfNeeded();
        }
        _otaInProgress = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        _otaInProgress = false;
        OtaGuard::setActive(false);
        remountFsIfNeeded();
        Serial.println("[OTA] Update was aborted.");
    }
}
