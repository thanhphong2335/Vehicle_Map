#include "RouteManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cfloat>
#include <cstring>
#include <esp_system.h>

#include "GpsManager.h"
#include "SettingsStore.h"
#include "SyncGuards.h"

namespace {
// HTTP is intentional here: the OSRM public endpoint rejects the TLS handshake offered
// by this ESP32 Arduino build. Only the route API uses port 80.
constexpr char kOsrmBaseUrl[] = "http://router.project-osrm.org/route/v1/driving/";
constexpr char kOsrmHost[] = "router.project-osrm.org";
constexpr char kFallbackOsrmBaseUrl[] = "http://routing.openstreetmap.de/routed-car/route/v1/driving/";
constexpr char kFallbackOsrmHost[] = "routing.openstreetmap.de";
constexpr uint32_t kRouteRequestTimeoutMs = 15000;
constexpr uint32_t kRouteRetryMs = 15000;
constexpr uint32_t kRerouteCooldownMs = 15000;
constexpr float kStepAdvanceDistanceM = 30.0F;
constexpr float kArrivalDistanceM = 25.0F;
constexpr float kOffRouteDistanceM = 60.0F;
constexpr uint8_t kOffRouteFixCount = 3;
constexpr uint32_t kRouteTaskStack = 8192;
constexpr UBaseType_t kRouteTaskPriority = 2;
constexpr BaseType_t kRouteTaskCore = 0;

class RouteLock {
public:
    explicit RouteLock(SemaphoreHandle_t mutex, TickType_t ticks = pdMS_TO_TICKS(500))
        : _mutex(mutex) {
        if (_mutex != nullptr) {
            _acquired = (xSemaphoreTakeRecursive(_mutex, ticks) == pdTRUE);
        } else {
            _acquired = true;
        }
    }
    ~RouteLock() {
        if (_acquired && _mutex != nullptr) {
            xSemaphoreGiveRecursive(_mutex);
        }
    }
    bool locked() const { return _acquired; }

private:
    SemaphoreHandle_t _mutex;
    bool _acquired = false;
};

Maneuver parseManeuver(const char* typeValue, const char* modifierValue) {
    String type(typeValue == nullptr ? "" : typeValue);
    String modifier(modifierValue == nullptr ? "" : modifierValue);
    type.toLowerCase();
    modifier.toLowerCase();
    if (type == "arrive") return Maneuver::Arrive;
    if (type.indexOf("roundabout") >= 0 || type.indexOf("rotary") >= 0) return Maneuver::Roundabout;
    if (modifier.indexOf("uturn") >= 0 || modifier.indexOf("u turn") >= 0) return Maneuver::UTurn;
    if (modifier.indexOf("left") >= 0) return Maneuver::Left;
    if (modifier.indexOf("right") >= 0) return Maneuver::Right;
    return Maneuver::Straight;
}

void copyUtf8RoadName(char* destination, size_t capacity, const char* source) {
    if (capacity == 0) return;
    if (source == nullptr || source[0] == '\0') source = "Unnamed road";
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
        while (length > 0 && (static_cast<uint8_t>(source[length]) & 0xC0) == 0x80) {
            --length;
        }
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

bool isCrashRecoveryReset(esp_reset_reason_t reason) {
    return reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_WDT ||
           reason == ESP_RST_PANIC ||
           reason == ESP_RST_BROWNOUT;
}
} // namespace

RouteManager::RouteManager(GpsManager* gps, SettingsStore* settings)
    : _gps(gps), _settings(settings) {
    _routePoints.reserve(kMaximumRoutePoints);
    _routeSteps.reserve(kMaximumRouteSteps);
}

RouteManager::~RouteManager() {
    if (_routeTask != nullptr) {
        vTaskDelete(_routeTask);
        _routeTask = nullptr;
    }
    if (_routeQueue != nullptr) {
        vQueueDelete(_routeQueue);
        _routeQueue = nullptr;
    }
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

void RouteManager::begin(NavigationUpdateCallback callback) {
    _callback = callback;
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateRecursiveMutex();
    }
    if (_routeQueue == nullptr) {
        _routeQueue = xQueueCreate(2, sizeof(RouteFetchJob));
    }
    if (_routeTask == nullptr && _routeQueue != nullptr) {
        xTaskCreatePinnedToCore(
            routeTaskThunk,
            "RouteTask",
            kRouteTaskStack,
            this,
            kRouteTaskPriority,
            &_routeTask,
            kRouteTaskCore);
    }

    const esp_reset_reason_t resetReason = esp_reset_reason();
    const bool allowAutoResume = isCrashRecoveryReset(resetReason);
    bool shouldClearNvsActive = false;

    {
        RouteLock lock(_mutex);
        _destination = _settings != nullptr ? _settings->loadDestination() : DestinationData{};
        if (_destination.valid && _destination.active) {
            if (allowAutoResume) {
                _state = RouteState::WaitingGps;
                Serial.printf("[ROUTE] Crash/WDT recovery reset (reason=%d): resuming active destination.\n",
                              static_cast<int>(resetReason));
            } else {
                _destination.active = false;
                _state = RouteState::Idle;
                shouldClearNvsActive = true;
                Serial.printf("[ROUTE] Clean boot (reason=%d): cleared stale active destination, staying Idle.\n",
                              static_cast<int>(resetReason));
            }
        } else {
            _state = RouteState::Idle;
        }
    }
    if (shouldClearNvsActive && _settings != nullptr) {
        _settings->setDestinationActive(false);
    }
    if (_state == RouteState::WaitingGps) {
        const GpsSnapshot position = _gps != nullptr ? _gps->snapshot() : GpsSnapshot{};
        emitNavigation(NavigationState::Start, "RESUMING ROUTE", &position);
    }
}

bool RouteManager::setDestination(const DestinationData& destination, String* error) {
    if (!destination.valid || destination.latitude < -90.0 || destination.latitude > 90.0 ||
        destination.longitude < -180.0 || destination.longitude > 180.0) {
        if (error != nullptr) *error = "invalid_destination";
        return false;
    }

    DestinationData saved = destination;
    saved.vehicle = saved.vehicle == "car" ? "car" : "motorcycle";
    if (_settings != nullptr && !_settings->saveDestination(saved)) {
        if (error != nullptr) *error = "destination_save_failed";
        return false;
    }

    RouteLock lock(_mutex);
    if (!lock.locked()) {
        if (error != nullptr) *error = "route_lock_timeout";
        return false;
    }
    _destination = saved;
    ++_generation;
    _pendingResult.ready = false;
    _fetchInFlight = false;
    _hasRoute = false;
    _rerouteRequested = false;
    _offRouteFixes = 0;
    _lastError = "";
    _state = saved.active ? RouteState::WaitingGps : RouteState::Idle;
    _nextRouteAttemptMs = 0;
    _pendingControl = saved.active ? PendingControl::StartDestination : PendingControl::Stop;
    return true;
}

void RouteManager::stop() {
    if (_settings != nullptr) _settings->setDestinationActive(false);

    RouteLock lock(_mutex);
    if (!lock.locked()) return;
    _destination.active = false;
    ++_generation;
    _pendingResult.ready = false;
    _fetchInFlight = false;
    _hasRoute = false;
    _rerouteRequested = false;
    _state = RouteState::Idle;
    _pendingControl = PendingControl::Stop;
}

void RouteManager::requestReroute() {
    RouteLock lock(_mutex);
    if (!lock.locked() || !_destination.valid || !_destination.active) return;
    ++_generation;
    _pendingResult.ready = false;
    _fetchInFlight = false;
    _rerouteRequested = true;
    _state = RouteState::OffRoute;
    _nextRouteAttemptMs = 0;
    _pendingControl = PendingControl::Reroute;
}

void RouteManager::applyPendingControlAndResult(const GpsSnapshot& position) {
    PendingControl control = PendingControl::None;
    bool hasResult = false;
    PendingRouteResult resultCopy;

    {
        RouteLock lock(_mutex);
        if (!lock.locked()) return;
        control = _pendingControl;
        _pendingControl = PendingControl::None;

        if (_pendingResult.ready) {
            if (_pendingResult.generation == _generation) {
                hasResult = true;
                resultCopy.ok = _pendingResult.ok;
                resultCopy.isReroute = _pendingResult.isReroute;
                resultCopy.generation = _pendingResult.generation;
                resultCopy.distanceM = _pendingResult.distanceM;
                resultCopy.durationS = _pendingResult.durationS;
                strncpy(resultCopy.error, _pendingResult.error, sizeof(resultCopy.error) - 1);
                resultCopy.points.swap(_pendingResult.points);
                resultCopy.steps.swap(_pendingResult.steps);
                resultCopy.startPosition = _pendingResult.startPosition;
            }
            _pendingResult.ready = false;
            _pendingResult.points.clear();
            _pendingResult.steps.clear();
        }
    }

    if (control == PendingControl::Stop) {
        _routePoints.clear();
        _routeSteps.clear();
        _currentStepIndex = 0;
        _nearestRouteIndex = 0;
        ++_routeRevision;
        NavigationData data;
        data.state = NavigationState::Stop;
        {
            RouteLock lock(_mutex);
            data.sequence = ++_sequence;
            _lastUpdateMs = millis();
            _currentNavigation = data;
        }
        if (_callback != nullptr) _callback(data);
        return;
    }

    if (control == PendingControl::StartDestination) {
        _routePoints.clear();
        _routeSteps.clear();
        _currentStepIndex = 0;
        _nearestRouteIndex = 0;
        ++_routeRevision;
        emitNavigation(NavigationState::Start, "WAITING FOR GPS", &position);
    } else if (control == PendingControl::Reroute) {
        emitNavigation(NavigationState::OffRoute, "REROUTE REQUESTED", &position);
    }

    if (hasResult) {
        if (resultCopy.ok) {
            _routePoints.swap(resultCopy.points);
            _routeSteps.swap(resultCopy.steps);
            _routeDistanceM = resultCopy.distanceM;
            _routeDurationS = resultCopy.durationS;
            _currentStepIndex = 0;
            while (_currentStepIndex + 1 < _routeSteps.size() && _routeSteps[_currentStepIndex].depart) {
                ++_currentStepIndex;
            }
            _nearestRouteIndex = 0;
            {
                RouteLock lock(_mutex);
                _hasRoute = true;
                ++_routeRevision;
                _lastError = "";
                _state = RouteState::Navigating;
                _rerouteRequested = false;
                _offRouteFixes = 0;
                _lastRerouteMs = millis();
            }
            emitNavigation(NavigationState::Start, "NAVIGATION", &position);
        } else {
            {
                RouteLock lock(_mutex);
                _lastError = resultCopy.error;
                _state = RouteState::Error;
                _nextRouteAttemptMs = millis() + kRouteRetryMs;
                if (resultCopy.isReroute) {
                    _lastRerouteMs = millis();
                }
            }
            emitNavigation(NavigationState::OffRoute, "ROUTE ERROR - RETRY", &position);
        }
    }
}

void RouteManager::loop(bool internetConnected) {
    if (_gps == nullptr) return;
    const GpsSnapshot position = _gps->snapshot();

    applyPendingControlAndResult(position);

    DestinationData dest;
    bool hasRouteVal = false;
    bool rerouteVal = false;
    bool inFlight = false;
    uint32_t nextAttempt = 0;
    uint32_t gen = 0;
    {
        RouteLock lock(_mutex);
        if (!lock.locked()) return;
        dest = _destination;
        hasRouteVal = _hasRoute;
        rerouteVal = _rerouteRequested;
        inFlight = _fetchInFlight;
        nextAttempt = _nextRouteAttemptMs;
        gen = _generation;
    }

    if (!dest.valid || !dest.active) return;

    if (!position.valid) {
        {
            RouteLock lock(_mutex);
            _state = RouteState::WaitingGps;
        }
        if (millis() - _lastWaitingDisplayMs >= 3000) {
            _lastWaitingDisplayMs = millis();
            emitNavigation(NavigationState::Update, "WAITING FOR GPS", &position);
        }
        return;
    }

    const uint32_t fixSequence = _gps->fixSequence();
    const bool newFix = fixSequence != _lastFixSequence;
    if (newFix) _lastFixSequence = fixSequence;

    const bool routeNeeded = !hasRouteVal || rerouteVal;
    if (routeNeeded && !inFlight && !OtaGuard::isActive() &&
        static_cast<int32_t>(millis() - nextAttempt) >= 0) {
        if (!internetConnected) {
            {
                RouteLock lock(_mutex);
                _state = rerouteVal ? RouteState::OffRoute : RouteState::WaitingGps;
            }
            if (newFix) emitNavigation(NavigationState::OffRoute, "OFFLINE - WAITING", &position);
            return;
        }

        RouteFetchJob job{};
        job.startLat = position.latitude;
        job.startLon = position.longitude;
        job.destLat = dest.latitude;
        job.destLon = dest.longitude;
        job.generation = gen;
        job.isReroute = rerouteVal;

        if (_routeQueue != nullptr && xQueueSend(_routeQueue, &job, 0) == pdTRUE) {
            {
                RouteLock lock(_mutex);
                _fetchInFlight = true;
                _state = RouteState::Requesting;
                if (rerouteVal) {
                    _lastRerouteMs = millis();
                }
            }
            emitNavigation(rerouteVal ? NavigationState::OffRoute : NavigationState::Start,
                           "CALCULATING ROUTE",
                           &position);
        }
        return;
    }

    if (hasRouteVal && newFix) {
        updateForPosition(position, internetConnected);
    }
}

void RouteManager::routeTaskThunk(void* parameter) {
    auto* self = static_cast<RouteManager*>(parameter);
    self->routeWorkerLoop();
}

void RouteManager::routeWorkerLoop() {
    RouteFetchJob job{};
    for (;;) {
        if (xQueueReceive(_routeQueue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (OtaGuard::isActive()) {
            RouteLock lock(_mutex);
            if (lock.locked() && job.generation == _generation) {
                _fetchInFlight = false;
                _nextRouteAttemptMs = millis() + 2000;
            }
            continue;
        }

        std::vector<RoutePoint> points;
        std::vector<RouteStep> steps;
        points.reserve(kMaximumRoutePoints);
        steps.reserve(kMaximumRouteSteps);
        float distanceM = 0.0F;
        float durationS = 0.0F;
        char error[64]{};

        const bool ok = fetchRouteInternal(
            job, points, steps, distanceM, durationS, error, sizeof(error));

        RouteLock lock(_mutex);
        if (lock.locked()) {
            _fetchInFlight = false;
            if (job.generation == _generation) {
                _pendingResult.ok = ok;
                _pendingResult.isReroute = job.isReroute;
                _pendingResult.generation = job.generation;
                _pendingResult.distanceM = distanceM;
                _pendingResult.durationS = durationS;
                strncpy(_pendingResult.error, error, sizeof(_pendingResult.error) - 1);
                _pendingResult.error[sizeof(_pendingResult.error) - 1] = '\0';
                _pendingResult.points.swap(points);
                _pendingResult.steps.swap(steps);
                _pendingResult.ready = true;
            }
        }
    }
}

bool RouteManager::fetchRouteInternal(const RouteFetchJob& job,
                                      std::vector<RoutePoint>& points,
                                      std::vector<RouteStep>& steps,
                                      float& distanceM,
                                      float& durationS,
                                      char* outError,
                                      size_t outErrorSize) const {
    struct RouteServer {
        const char* name;
        const char* host;
        const char* baseUrl;
    };
    const RouteServer servers[] = {
        {"fallback", kFallbackOsrmHost, kFallbackOsrmBaseUrl},
        {"primary", kOsrmHost, kOsrmBaseUrl},
    };

    String lastError = "osrm_no_server";
    for (const RouteServer& server : servers) {
        if (OtaGuard::isActive()) {
            lastError = "ota_in_progress";
            break;
        }

        NetTlsGate netGate(pdMS_TO_TICKS(15000));
        if (!netGate.acquired()) {
            lastError = "net_gate_timeout";
            continue;
        }

        IPAddress resolvedIp;
        if (WiFi.hostByName(server.host, resolvedIp) != 1) {
            lastError = String("osrm_") + server.name + "_dns_failed";
            Serial.printf("Route %s: DNS failed for %s\n", server.name, server.host);
            continue;
        }
        Serial.printf("Route %s: %s -> %s\n",
                      server.name,
                      server.host,
                      resolvedIp.toString().c_str());

        String url;
        url.reserve(290);
        url += server.baseUrl;
        url += String(job.startLon, 6);
        url += ',';
        url += String(job.startLat, 6);
        url += ';';
        url += String(job.destLon, 6);
        url += ',';
        url += String(job.destLat, 6);
        url += "?steps=true&overview=simplified&geometries=polyline6&alternatives=false";

        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(kRouteRequestTimeoutMs);
        http.setTimeout(kRouteRequestTimeoutMs);
        http.useHTTP10(true);
        if (!http.begin(client, url)) {
            lastError = String("osrm_") + server.name + "_begin_failed";
            Serial.printf("Route %s: HTTP begin failed\n", server.name);
            continue;
        }
        http.addHeader("Accept", "application/json");
        http.addHeader("User-Agent", "MotoMap-ESP32/1.0");

        const int status = http.GET();
        if (status != HTTP_CODE_OK) {
            const String detail = status < 0 ? HTTPClient::errorToString(status) : String("HTTP error");
            lastError = String("osrm_") + server.name + "_" + status;
            Serial.printf("Route %s: GET %d (%s)\n", server.name, status, detail.c_str());
            http.end();
            continue;
        }

        points.clear();
        steps.clear();
        String parseError;
        const bool ok = parseRouteResponse(http.getStream(), points, steps, distanceM, durationS, parseError);
        http.end();
        if (!ok) {
            lastError = String("osrm_") + server.name + "_" + parseError;
            Serial.printf("Route %s: response parse failed (%s)\n", server.name, parseError.c_str());
            continue;
        }

        Serial.printf("Route %s: OK, %.0fm, %.0fs\n", server.name, distanceM, durationS);
        if (outError != nullptr && outErrorSize > 0) outError[0] = '\0';
        return true;
    }

    if (outError != nullptr && outErrorSize > 0) {
        strncpy(outError, lastError.c_str(), outErrorSize - 1);
        outError[outErrorSize - 1] = '\0';
    }
    return false;
}

bool RouteManager::parseRouteResponse(Stream& stream,
                                      std::vector<RoutePoint>& points,
                                      std::vector<RouteStep>& steps,
                                      float& distanceM,
                                      float& durationS,
                                      String& error) const {
    JsonDocument filter;
    filter["code"] = true;
    filter["routes"][0]["distance"] = true;
    filter["routes"][0]["duration"] = true;
    filter["routes"][0]["geometry"] = true;
    filter["routes"][0]["legs"][0]["steps"][0]["distance"] = true;
    filter["routes"][0]["legs"][0]["steps"][0]["duration"] = true;
    filter["routes"][0]["legs"][0]["steps"][0]["name"] = true;
    filter["routes"][0]["legs"][0]["steps"][0]["maneuver"]["type"] = true;
    filter["routes"][0]["legs"][0]["steps"][0]["maneuver"]["modifier"] = true;
    filter["routes"][0]["legs"][0]["steps"][0]["maneuver"]["location"] = true;

    JsonDocument document;
    const DeserializationError jsonError = deserializeJson(
        document,
        stream,
        DeserializationOption::Filter(filter));
    if (jsonError) {
        error = String("osrm_json_") + jsonError.c_str();
        return false;
    }
    if (String(document["code"] | "") != "Ok") {
        error = "osrm_no_route";
        return false;
    }

    JsonObject route = document["routes"][0];
    const char* geometryValue = route["geometry"] | "";
    const size_t geomLen = strlen(geometryValue);
    const char* polyErr = nullptr;
    if (geomLen == 0 || !NavMath::decodePolyline6(geometryValue, geomLen, points, kMaximumRoutePoints, &polyErr)) {
        error = polyErr != nullptr ? polyErr : "route_geometry_missing";
        return false;
    }

    JsonArray jsonSteps = route["legs"][0]["steps"].as<JsonArray>();
    if (jsonSteps.isNull() || jsonSteps.size() == 0 || jsonSteps.size() > kMaximumRouteSteps) {
        error = jsonSteps.size() > kMaximumRouteSteps ? "route_too_many_steps" : "route_steps_missing";
        return false;
    }

    for (JsonObject jsonStep : jsonSteps) {
        JsonArray location = jsonStep["maneuver"]["location"].as<JsonArray>();
        if (location.size() < 2) continue;
        RouteStep step;
        step.longitude = location[0] | 0.0F;
        step.latitude = location[1] | 0.0F;
        step.distanceM = jsonStep["distance"] | 0.0F;
        step.durationS = jsonStep["duration"] | 0.0F;
        const char* type = jsonStep["maneuver"]["type"] | "";
        const char* modifier = jsonStep["maneuver"]["modifier"] | "";
        step.depart = strcmp(type, "depart") == 0;
        step.arrive = strcmp(type, "arrive") == 0;
        step.maneuver = parseManeuver(type, modifier);
        copyUtf8RoadName(step.roadName, sizeof(step.roadName), jsonStep["name"] | "");
        steps.push_back(step);
    }
    if (steps.empty()) {
        error = "route_steps_invalid";
        return false;
    }

    distanceM = route["distance"] | 0.0F;
    durationS = route["duration"] | 0.0F;
    matchStepRouteIndexes(steps, points);
    return true;
}

void RouteManager::matchStepRouteIndexes(std::vector<RouteStep>& steps,
                                         const std::vector<RoutePoint>& points) const {
    size_t searchStart = 0;
    for (RouteStep& step : steps) {
        float bestDistance = FLT_MAX;
        size_t bestIndex = searchStart;
        for (size_t index = searchStart; index < points.size(); ++index) {
            const float distance = NavMath::distanceMeters(step.latitude,
                                                           step.longitude,
                                                           points[index].latitude,
                                                           points[index].longitude);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        step.routeIndex = static_cast<uint16_t>(bestIndex);
        searchStart = bestIndex;
    }
}

void RouteManager::updateForPosition(const GpsSnapshot& position, bool internetConnected) {
    if (_routePoints.size() < 2 || _routeSteps.empty()) return;

    const size_t maxSeg = _routePoints.size() - 1;
    const size_t winStart = _nearestRouteIndex > 8 ? (_nearestRouteIndex - 8) : 0;
    const size_t winEnd = (_nearestRouteIndex + 48 < maxSeg) ? (_nearestRouteIndex + 48) : maxSeg;

    float nearestDistance = FLT_MAX;
    size_t nearestIndex = _nearestRouteIndex < maxSeg ? _nearestRouteIndex : 0;
    for (size_t index = winStart; index < winEnd; ++index) {
        const float distance = NavMath::pointToSegmentMeters(position.latitude,
                                                               position.longitude,
                                                               _routePoints[index].latitude,
                                                               _routePoints[index].longitude,
                                                               _routePoints[index + 1].latitude,
                                                               _routePoints[index + 1].longitude);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestIndex = index;
        }
    }

    // Fall back to full scan only if vehicle jumped far outside the local window
    if (nearestDistance > kOffRouteDistanceM && (winStart > 0 || winEnd < maxSeg)) {
        for (size_t index = 0; index < maxSeg; ++index) {
            if (index >= winStart && index < winEnd) continue;
            const float distance = NavMath::pointToSegmentMeters(position.latitude,
                                                                   position.longitude,
                                                                   _routePoints[index].latitude,
                                                                   _routePoints[index].longitude,
                                                                   _routePoints[index + 1].latitude,
                                                                   _routePoints[index + 1].longitude);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = index;
            }
        }
    }
    _nearestRouteIndex = static_cast<uint16_t>(nearestIndex);

    DestinationData destSnapshot;
    {
        RouteLock lock(_mutex);
        destSnapshot = _destination;
    }

    const float destinationDistance = NavMath::distanceMeters(position.latitude,
                                                              position.longitude,
                                                              destSnapshot.latitude,
                                                              destSnapshot.longitude);
    if (destinationDistance <= kArrivalDistanceM) {
        {
            RouteLock lock(_mutex);
            _state = RouteState::Arrived;
            _destination.active = false;
            _hasRoute = false;
        }
        if (_settings != nullptr) _settings->setDestinationActive(false);
        emitNavigation(NavigationState::Arrived, "ARRIVED", &position);
        return;
    }

    RouteStep* step = &_routeSteps[_currentStepIndex];
    float distanceToTurn = NavMath::distanceMeters(position.latitude,
                                                   position.longitude,
                                                   step->latitude,
                                                   step->longitude);
    while (_currentStepIndex + 1 < _routeSteps.size() &&
           (distanceToTurn <= kStepAdvanceDistanceM ||
            _nearestRouteIndex > step->routeIndex + 2)) {
        ++_currentStepIndex;
        step = &_routeSteps[_currentStepIndex];
        distanceToTurn = NavMath::distanceMeters(position.latitude,
                                                 position.longitude,
                                                 step->latitude,
                                                 step->longitude);
    }

    if (nearestDistance > kOffRouteDistanceM) {
        if (_offRouteFixes < 255) ++_offRouteFixes;
    } else {
        _offRouteFixes = 0;
    }

    if (_offRouteFixes >= kOffRouteFixCount &&
        millis() - _lastRerouteMs >= kRerouteCooldownMs) {
        {
            RouteLock lock(_mutex);
            _rerouteRequested = true;
            _state = RouteState::OffRoute;
            _nextRouteAttemptMs = 0;
        }
        emitNavigation(NavigationState::OffRoute,
                       internetConnected ? "OFF ROUTE" : "OFFLINE - WAITING",
                       &position);
        return;
    }

    {
        RouteLock lock(_mutex);
        _state = RouteState::Navigating;
    }
    emitNavigation(NavigationState::Update,
                   internetConnected ? "NAVIGATION" : "NAVIGATION OFFLINE",
                   &position);
}

void RouteManager::emitNavigation(NavigationState navigationState,
                                  const String& status,
                                  const GpsSnapshot* position) {
    NavigationData data;
    data.state = navigationState;
    data.wifiConnected = WiFi.status() == WL_CONNECTED;
    if (position != nullptr) {
        data.gpsValid = position->valid;
        data.satellites = position->satellites;
        data.speedKmh = position->speedKmh;
        data.latitude = position->latitude;
        data.longitude = position->longitude;
        data.courseDeg = position->courseDeg;
    }
    data.statusMessage = status;

    bool hasRouteVal = false;
    String fallbackLabel;
    {
        RouteLock lock(_mutex);
        data.vehicle = _destination.vehicle;
        data.sequence = ++_sequence;
        hasRouteVal = _hasRoute;
        fallbackLabel = _destination.label;
    }

    if (hasRouteVal && !_routeSteps.empty()) {
        const RouteStep& step = _routeSteps[_currentStepIndex];
        data.maneuver = step.maneuver;
        data.roadName = String(step.roadName);
        if (position != nullptr && position->valid) {
            data.distanceToTurnM = static_cast<uint32_t>(NavMath::distanceMeters(position->latitude,
                                                                                 position->longitude,
                                                                                 step.latitude,
                                                                                 step.longitude) + 0.5F);
        }
        float remainingDistance = data.distanceToTurnM;
        float remainingDuration = 0.0F;
        for (size_t index = _currentStepIndex; index < _routeSteps.size(); ++index) {
            remainingDistance += _routeSteps[index].distanceM;
            remainingDuration += _routeSteps[index].durationS;
        }
        data.remainingDistanceM = static_cast<uint32_t>(remainingDistance + 0.5F);
        data.etaSeconds = static_cast<uint32_t>(remainingDuration + 0.5F);
    } else {
        data.roadName = fallbackLabel.length() ? fallbackLabel : "Waiting for route";
    }

    {
        RouteLock lock(_mutex);
        _currentNavigation = data;
        _lastUpdateMs = millis();
    }
    if (_callback != nullptr) _callback(data);
}

RouteState RouteManager::state() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _state : RouteState::Idle;
}

DestinationData RouteManager::destination() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _destination : DestinationData{};
}

String RouteManager::lastError() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _lastError : String();
}

bool RouteManager::hasRoute() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _hasRoute : false;
}

uint32_t RouteManager::sequence() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _sequence : 0;
}

uint32_t RouteManager::lastUpdateMillis() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _lastUpdateMs : 0;
}

NavigationData RouteManager::currentNavigation() const {
    RouteLock lock(_mutex);
    return lock.locked() ? _currentNavigation : NavigationData{};
}

void RouteManager::getStatusSnapshot(StatusSnapshot& outSnapshot) const {
    RouteLock lock(_mutex);
    if (!lock.locked()) return;
    outSnapshot.state = _state;
    outSnapshot.destination = _destination;
    outSnapshot.navigation = _currentNavigation;
    outSnapshot.lastError = _lastError;
    outSnapshot.sequence = _sequence;
    outSnapshot.lastUpdateMs = _lastUpdateMs;
    outSnapshot.hasRoute = _hasRoute;
}

const std::vector<RouteManager::RoutePoint>& RouteManager::routePoints() const { return _routePoints; }
uint16_t RouteManager::nearestRouteIndex() const { return _nearestRouteIndex; }
uint32_t RouteManager::routeRevision() const { return _routeRevision; }
