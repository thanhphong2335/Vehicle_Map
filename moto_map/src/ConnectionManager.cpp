#include "ConnectionManager.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "DisplayManager.h"
#include "GpsManager.h"
#include "MediaManager.h"
#include "ModeController.h"
#include "RouteManager.h"
#include "SettingsStore.h"

namespace {
constexpr char kApSsid[] = "MotoMap-ESP32";
constexpr char kApPassword[] = "motomap123";
constexpr char kMdnsName[] = "motomap";
constexpr uint32_t kStationTimeoutMs = 20000;
constexpr uint32_t kReconnectDelaysMs[] = {5000, 15000, 30000};

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
                                     SettingsStore* settings)
    : _display(display),
      _media(media),
      _modeController(modeController),
      _gps(gps),
      _route(route),
      _settings(settings) {}

void ConnectionManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed in ConnectionManager");
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
    // Starting WebServer before WiFi.mode()/WiFi.begin() can trigger an
    // lwIP "Invalid mbox" assertion on ESP32.
    _server.begin();
}

void ConnectionManager::startStation() {
    if (_stationSsid.length() == 0 &&
        (_settings == nullptr || !_settings->loadWifi(_stationSsid, _stationPassword))) {
        startAccessPoint();
        return;
    }

    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }
    WiFi.disconnect(true, false);
    // Keep the setup portal available even after joining the user's router.
    // This lets a phone change Wi-Fi credentials at 192.168.4.1 without first
    // taking the old router offline.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApSsid, kApPassword);
    WiFi.begin(_stationSsid.c_str(), _stationPassword.c_str());
    _networkState = NetworkState::Connecting;
    _connectionStartedMs = millis();
    _nextReconnectMs = millis() + kReconnectDelaysMs[0];
    _reconnectAttempt = 0;
    Serial.printf("Connecting to iPhone hotspot: %s\n", _stationSsid.c_str());
    if (_display != nullptr && !navigationActive() && (_media == nullptr || !_media->hasTestImage())) {
        _display->showIdle(String("Connecting ") + _stationSsid);
    }
}

void ConnectionManager::startAccessPoint() {
    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }

    const bool hasStation = _settings != nullptr &&
                            _settings->loadWifi(_stationSsid, _stationPassword);
    WiFi.disconnect(true, false);
    WiFi.mode(hasStation ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);
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
    if (_display != nullptr && !navigationActive() && (_media == nullptr || !_media->hasTestImage())) {
        _display->showIdle("AP SETUP ACTIVE");
    }
}

void ConnectionManager::handleStationConnected() {
    // Turn off the setup AP to save ~100mA and cut thermal generation significantly.
    // If station connection is ever lost, startAccessPoint() turns the AP back on automatically.
    WiFi.mode(WIFI_STA);
    _networkState = NetworkState::Station;
    _connectionStartedMs = millis();
    _reconnectAttempt = 0;
    if (_mdnsStarted) MDNS.end();
    _mdnsStarted = MDNS.begin(kMdnsName);
    if (_mdnsStarted) MDNS.addService("http", "tcp", 80);

    Serial.printf("WiFi connected. IP: %s, web: http://motomap.local/\n",
                  WiFi.localIP().toString().c_str());
    if (_display != nullptr && !navigationActive() && (_media == nullptr || !_media->hasTestImage())) {
        _display->showIdle(String("http://") + WiFi.localIP().toString() + "/");
    }
}

void ConnectionManager::scheduleStationSwitch(uint32_t delayMs) {
    if (_settings == nullptr || !_settings->loadWifi(_stationSsid, _stationPassword)) return;
    _stationSwitchAtMs = millis() + delayMs;
}

void ConnectionManager::updateNetwork() {
    const uint32_t now = millis();
    if (_stationSwitchAtMs != 0 && static_cast<int32_t>(now - _stationSwitchAtMs) >= 0) {
        _stationSwitchAtMs = 0;
        startStation();
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (_networkState != NetworkState::Station) handleStationConnected();
        return;
    }

    if (_networkState == NetworkState::Station) {
        _networkState = NetworkState::Connecting;
        _connectionStartedMs = now;
        _nextReconnectMs = now;
        _reconnectAttempt = 0;
    }

    if (_networkState == NetworkState::Connecting) {
        if (now - _connectionStartedMs >= kStationTimeoutMs) {
            startAccessPoint();
            return;
        }
        if (static_cast<int32_t>(now - _nextReconnectMs) >= 0) {
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

    _server.on("/mode/media", HTTP_POST, [this]() {
        if (navigationActive()) {
            sendError("navigation_has_priority", 409);
            return;
        }
        if (_modeController != nullptr) _modeController->onMedia();
        sendJson("{\"ok\":true,\"mode\":\"MEDIA\"}");
    });
    _server.on("/mode/idle", HTTP_POST, [this]() {
        if (_media != nullptr) _media->clearTestImage();
        if (_modeController != nullptr) _modeController->onIdle();
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
        const bool ok = _media != nullptr && _media->handleJpeg(
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

    _server.onNotFound([this]() {
        if (_server.method() == HTTP_OPTIONS) {
            _server.sendHeader("Access-Control-Allow-Origin", "*");
            _server.sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
            _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
            _server.send(204);
            return;
        }
        sendError("not_found", 404);
    });
}

void ConnectionManager::handleStatus() {
    JsonDocument document;
    document["device"] = "MotoMap-ESP32";
    document["mode"] = _modeController != nullptr ? modeName(_modeController->mode()) : "UNKNOWN";
    document["route_state"] = _route != nullptr ? routeStateName(_route->state()) : "UNKNOWN";
    document["network_state"] = networkStateName();
    document["internet_connected"] = internetConnected();
    document["ip"] = internetConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    document["web_url"] = webAddress();
    document["rssi"] = internetConnected() ? WiFi.RSSI() : 0;
    document["uptime_ms"] = millis();
    document["heap_free"] = ESP.getFreeHeap();
    document["littlefs_total"] = LittleFS.totalBytes();
    document["littlefs_used"] = LittleFS.usedBytes();
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
        const DestinationData& destination = _route->destination();
        JsonObject jsonDestination = document["destination"].to<JsonObject>();
        jsonDestination["valid"] = destination.valid;
        jsonDestination["active"] = destination.active;
        jsonDestination["lat"] = destination.latitude;
        jsonDestination["lon"] = destination.longitude;
        jsonDestination["label"] = destination.label;
        jsonDestination["vehicle"] = destination.vehicle;
        document["navigation_sequence"] = _route->sequence();
        document["navigation_age_ms"] = _route->lastUpdateMillis() == 0
            ? 0
            : millis() - _route->lastUpdateMillis();
        document["route_error"] = _route->lastError();
        const NavigationData& navigation = _route->currentNavigation();
        JsonObject jsonNavigation = document["navigation"].to<JsonObject>();
        jsonNavigation["maneuver"] = maneuverName(navigation.maneuver);
        jsonNavigation["distance_to_turn_m"] = navigation.distanceToTurnM;
        jsonNavigation["remaining_distance_m"] = navigation.remainingDistanceM;
        jsonNavigation["eta_seconds"] = navigation.etaSeconds;
        jsonNavigation["road_name"] = navigation.roadName;
        jsonNavigation["status"] = navigation.statusMessage;
    }
    if (_media != nullptr) {
        JsonObject animation = document["animation"].to<JsonObject>();
        animation["valid"] = _media->hasAnimation();
        animation["frames"] = _media->animationFrameCount();
        animation["fps"] = _media->animationFps();
        animation["bytes"] = _media->animationBytes();
        document["media_frames"] = _media->frameCount();
        document["test_image_active"] = _media->hasTestImage();
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
        sendError(error, 400);
        return;
    }
    const String ssid = String(document["ssid"] | "");
    const String password = String(document["password"] | "");
    const bool connectNow = document["connect_now"] | false;
    if (_settings == nullptr || !_settings->saveWifi(ssid, password)) {
        sendError("invalid_wifi_config", 400);
        return;
    }
    _stationSsid = ssid;
    _stationPassword = password;
    if (connectNow || (WiFi.status() == WL_CONNECTED && WiFi.SSID() != ssid)) {
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

    if (_modeController != nullptr) _modeController->onMedia(false);
    sendJson("{\"ok\":true,\"display\":\"test_image\"}");
}

void ConnectionManager::handleTestImageUpload() {
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        _testImageUploadData = "";
        _testImageUploadData.reserve(128U * 1024U);
        _testImageUploadOk = !navigationActive();
        _testImageUploadError = _testImageUploadOk ? "" : "navigation_has_priority";
        return;
    }
    if (!_testImageUploadOk) return;
    if (upload.status == UPLOAD_FILE_WRITE) {
        if (_testImageUploadData.length() + upload.currentSize > 128U * 1024U) {
            _testImageUploadOk = false;
            _testImageUploadError = "jpeg_too_large";
            return;
        }
        _testImageUploadData.concat(reinterpret_cast<const char*>(upload.buf), upload.currentSize);
        return;
    }
    if (upload.status == UPLOAD_FILE_END) {
        _testImageUploadOk = _media != nullptr && _media->showTestImage(
            reinterpret_cast<const uint8_t*>(_testImageUploadData.c_str()), _testImageUploadData.length());
        if (!_testImageUploadOk && !_testImageUploadError.length()) {
            _testImageUploadError = "jpeg_decode_failed";
        }
        _testImageUploadData = "";
    }
}

void ConnectionManager::handleTestImageClear() {
    if (_media != nullptr) _media->clearTestImage();
    if (_modeController != nullptr) _modeController->onIdle("Image test stopped");
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
    if (_modeController != nullptr) _modeController->onIdle("Animation deleted");
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
    if (!LittleFS.exists(path)) {
        sendError("file_not_found", 404);
        return false;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        sendError("file_open_failed", 500);
        return false;
    }
    _server.sendHeader("Cache-Control", "no-store");
    _server.streamFile(file, contentType);
    file.close();
    return true;
}

void ConnectionManager::loop() {
    _server.handleClient();
    updateNetwork();
}
