#include "RouteManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cfloat>
#include <math.h>

#include "GpsManager.h"
#include "SettingsStore.h"

namespace {
// HTTP is intentional here: the OSRM server rejects the TLS handshake offered
// by this ESP32 Arduino build. The user accepted the unencrypted route request
// for this prototype, so only the route API uses port 80.
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
constexpr double kEarthRadiusM = 6371000.0;

double radiansFromDegrees(double degrees) {
    return degrees * DEG_TO_RAD;
}

float distanceMeters(double lat1, double lon1, double lat2, double lon2) {
    const double dLat = radiansFromDegrees(lat2 - lat1);
    const double dLon = radiansFromDegrees(lon2 - lon1);
    const double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
                     cos(radiansFromDegrees(lat1)) * cos(radiansFromDegrees(lat2)) *
                     sin(dLon / 2.0) * sin(dLon / 2.0);
    return static_cast<float>(kEarthRadiusM * 2.0 * atan2(sqrt(a), sqrt(1.0 - a)));
}

float pointToSegmentMeters(double latitude,
                           double longitude,
                           double startLatitude,
                           double startLongitude,
                           double endLatitude,
                           double endLongitude) {
    const double latitudeRadians = radiansFromDegrees(latitude);
    const double sx = radiansFromDegrees(startLongitude - longitude) * cos(latitudeRadians) * kEarthRadiusM;
    const double sy = radiansFromDegrees(startLatitude - latitude) * kEarthRadiusM;
    const double ex = radiansFromDegrees(endLongitude - longitude) * cos(latitudeRadians) * kEarthRadiusM;
    const double ey = radiansFromDegrees(endLatitude - latitude) * kEarthRadiusM;
    const double dx = ex - sx;
    const double dy = ey - sy;
    const double lengthSquared = dx * dx + dy * dy;
    double projection = lengthSquared > 0.0001 ? -(sx * dx + sy * dy) / lengthSquared : 0.0;
    if (projection < 0.0) projection = 0.0;
    if (projection > 1.0) projection = 1.0;
    const double px = sx + projection * dx;
    const double py = sy + projection * dy;
    return static_cast<float>(sqrt(px * px + py * py));
}

bool decodePolylineValue(const String& encoded, size_t& index, int32_t& value) {
    int32_t result = 0;
    uint8_t shift = 0;
    while (index < encoded.length()) {
        const int32_t byteValue = static_cast<int32_t>(encoded[index++]) - 63;
        if (byteValue < 0) return false;
        result |= (byteValue & 0x1F) << shift;
        shift += 5;
        if (byteValue < 0x20) {
            value = (result & 1) ? ~(result >> 1) : (result >> 1);
            return true;
        }
        if (shift > 30) return false;
    }
    return false;
}

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
}

RouteManager::RouteManager(GpsManager* gps, SettingsStore* settings)
    : _gps(gps), _settings(settings) {
    _routePoints.reserve(kMaximumRoutePoints);
    _routeSteps.reserve(kMaximumRouteSteps);
}

void RouteManager::begin(NavigationUpdateCallback callback) {
    _callback = callback;
    _destination = _settings != nullptr ? _settings->loadDestination() : DestinationData{};
    _state = _destination.valid && _destination.active ? RouteState::WaitingGps : RouteState::Idle;
    if (_state == RouteState::WaitingGps) {
        const GpsSnapshot position = _gps != nullptr ? _gps->snapshot() : GpsSnapshot{};
        emitNavigation(NavigationState::Start, "WAITING FOR GPS", &position);
    }
}

bool RouteManager::setDestination(const DestinationData& destination, String* error) {
    if (!destination.valid || destination.latitude < -90.0 || destination.latitude > 90.0 ||
        destination.longitude < -180.0 || destination.longitude > 180.0) {
        if (error != nullptr) *error = "invalid_destination";
        return false;
    }

    DestinationData saved = destination;
    saved.active = true;
    saved.vehicle = saved.vehicle == "car" ? "car" : "motorcycle";
    if (_settings != nullptr && !_settings->saveDestination(saved)) {
        if (error != nullptr) *error = "destination_save_failed";
        return false;
    }

    _destination = saved;
    _routePoints.clear();
    _routeSteps.clear();
    ++_routeRevision;
    _currentStepIndex = 0;
    _hasRoute = false;
    _rerouteRequested = false;
    _offRouteFixes = 0;
    _lastError = "";
    _state = RouteState::WaitingGps;
    _nextRouteAttemptMs = 0;
    const GpsSnapshot position = _gps != nullptr ? _gps->snapshot() : GpsSnapshot{};
    emitNavigation(NavigationState::Start, "WAITING FOR GPS", &position);
    return true;
}

void RouteManager::stop() {
    _destination.active = false;
    if (_settings != nullptr) _settings->setDestinationActive(false);
    _routePoints.clear();
    _routeSteps.clear();
    ++_routeRevision;
    _hasRoute = false;
    _rerouteRequested = false;
    _state = RouteState::Idle;
    NavigationData data;
    data.state = NavigationState::Stop;
    data.sequence = ++_sequence;
    _lastUpdateMs = millis();
    _currentNavigation = data;
    if (_callback != nullptr) _callback(data);
}

void RouteManager::requestReroute() {
    if (!_destination.valid || !_destination.active) return;
    _rerouteRequested = true;
    _state = RouteState::OffRoute;
    _nextRouteAttemptMs = 0;
    const GpsSnapshot position = _gps != nullptr ? _gps->snapshot() : GpsSnapshot{};
    emitNavigation(NavigationState::OffRoute, "REROUTE REQUESTED", &position);
}

void RouteManager::loop(bool internetConnected) {
    if (!_destination.valid || !_destination.active || _gps == nullptr) return;

    const GpsSnapshot position = _gps->snapshot();
    if (!position.valid) {
        _state = RouteState::WaitingGps;
        if (millis() - _lastWaitingDisplayMs >= 3000) {
            _lastWaitingDisplayMs = millis();
            emitNavigation(NavigationState::Update, "WAITING FOR GPS", &position);
        }
        return;
    }

    const uint32_t fixSequence = _gps->fixSequence();
    const bool newFix = fixSequence != _lastFixSequence;
    if (newFix) _lastFixSequence = fixSequence;

    const bool routeNeeded = !_hasRoute || _rerouteRequested;
    if (routeNeeded && static_cast<int32_t>(millis() - _nextRouteAttemptMs) >= 0) {
        if (!internetConnected) {
            _state = _rerouteRequested ? RouteState::OffRoute : RouteState::WaitingGps;
            if (newFix) emitNavigation(NavigationState::OffRoute, "OFFLINE - WAITING", &position);
            return;
        }

        _state = RouteState::Requesting;
        emitNavigation(_rerouteRequested ? NavigationState::OffRoute : NavigationState::Start,
                       "CALCULATING ROUTE",
                       &position);
        String error;
        if (fetchRoute(position, error)) {
            _state = RouteState::Navigating;
            _rerouteRequested = false;
            _offRouteFixes = 0;
            _lastRerouteMs = millis();
            emitNavigation(NavigationState::Start, "NAVIGATION", &position);
        } else {
            _lastError = error;
            _state = RouteState::Error;
            _nextRouteAttemptMs = millis() + kRouteRetryMs;
            emitNavigation(NavigationState::OffRoute, "ROUTE ERROR - RETRY", &position);
        }
        return;
    }

    if (_hasRoute && newFix) {
        updateForPosition(position, internetConnected);
    }
}

bool RouteManager::fetchRoute(const GpsSnapshot& position, String& error) {
    struct RouteServer {
        const char* name;
        const char* host;
        const char* baseUrl;
    };
    // Both endpoints expose the OSRM-compatible route API. Keep the retry so
    // a temporary failure at one hostname does not stop navigation.
    const RouteServer servers[] = {
        {"fallback", kFallbackOsrmHost, kFallbackOsrmBaseUrl},
        {"primary", kOsrmHost, kOsrmBaseUrl},
    };

    String lastError = "osrm_no_server";
    for (const RouteServer& server : servers) {
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
        url += String(position.longitude, 6);
        url += ',';
        url += String(position.latitude, 6);
        url += ';';
        url += String(_destination.longitude, 6);
        url += ',';
        url += String(_destination.latitude, 6);
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

        std::vector<RoutePoint> points;
        std::vector<RouteStep> steps;
        points.reserve(kMaximumRoutePoints);
        steps.reserve(kMaximumRouteSteps);
        float distanceM = 0.0F;
        float durationS = 0.0F;
        String parseError;
        const bool ok = parseRouteResponse(http.getStream(), points, steps, distanceM, durationS, parseError);
        http.end();
        if (!ok) {
            lastError = String("osrm_") + server.name + "_" + parseError;
            Serial.printf("Route %s: response parse failed (%s)\n", server.name, parseError.c_str());
            continue;
        }

        _routePoints.swap(points);
        _routeSteps.swap(steps);
        _routeDistanceM = distanceM;
        _routeDurationS = durationS;
        _currentStepIndex = 0;
        while (_currentStepIndex + 1 < _routeSteps.size() && _routeSteps[_currentStepIndex].depart) {
            ++_currentStepIndex;
        }
        _nearestRouteIndex = 0;
        _hasRoute = true;
        ++_routeRevision;
        _lastError = "";
        Serial.printf("Route %s: OK, %.0fm, %.0fs\n", server.name, distanceM, durationS);
        return true;
    }

    error = lastError;
    return false;
}

bool RouteManager::parseRouteResponse(Stream& stream,
                                      std::vector<RoutePoint>& points,
                                      std::vector<RouteStep>& steps,
                                      float& distanceM,
                                      float& durationS,
                                      String& error) {
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
    if (geometryValue[0] == '\0' || !decodePolyline6(String(geometryValue), points, error)) {
        if (error.length() == 0) error = "route_geometry_missing";
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

bool RouteManager::decodePolyline6(const String& encoded,
                                   std::vector<RoutePoint>& points,
                                   String& error) const {
    size_t index = 0;
    size_t pointCount = 0;
    int32_t latitude = 0;
    int32_t longitude = 0;
    while (index < encoded.length()) {
        int32_t deltaLatitude = 0;
        int32_t deltaLongitude = 0;
        if (!decodePolylineValue(encoded, index, deltaLatitude) ||
            !decodePolylineValue(encoded, index, deltaLongitude)) {
            error = "invalid_polyline6";
            return false;
        }
        latitude += deltaLatitude;
        longitude += deltaLongitude;
        ++pointCount;
    }
    if (pointCount < 2) {
        error = "route_geometry_short";
        return false;
    }

    const size_t stride = (pointCount + kMaximumRoutePoints - 1) / kMaximumRoutePoints;
    index = 0;
    latitude = 0;
    longitude = 0;
    size_t rawIndex = 0;
    RoutePoint finalPoint;
    while (index < encoded.length()) {
        int32_t deltaLatitude = 0;
        int32_t deltaLongitude = 0;
        decodePolylineValue(encoded, index, deltaLatitude);
        decodePolylineValue(encoded, index, deltaLongitude);
        latitude += deltaLatitude;
        longitude += deltaLongitude;
        finalPoint.latitude = static_cast<float>(latitude / 1000000.0);
        finalPoint.longitude = static_cast<float>(longitude / 1000000.0);
        if (rawIndex % stride == 0 && points.size() < kMaximumRoutePoints) {
            points.push_back(finalPoint);
        }
        ++rawIndex;
    }
    if (points.empty()) {
        error = "route_geometry_empty";
        return false;
    }
    if (distanceMeters(points.back().latitude,
                       points.back().longitude,
                       finalPoint.latitude,
                       finalPoint.longitude) > 1.0F) {
        if (points.size() == kMaximumRoutePoints) points.back() = finalPoint;
        else points.push_back(finalPoint);
    }
    return points.size() >= 2;
}

void RouteManager::matchStepRouteIndexes(std::vector<RouteStep>& steps,
                                         const std::vector<RoutePoint>& points) const {
    size_t searchStart = 0;
    for (RouteStep& step : steps) {
        float bestDistance = FLT_MAX;
        size_t bestIndex = searchStart;
        for (size_t index = searchStart; index < points.size(); ++index) {
            const float distance = distanceMeters(step.latitude,
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

    float nearestDistance = FLT_MAX;
    size_t nearestIndex = 0;
    for (size_t index = 0; index + 1 < _routePoints.size(); ++index) {
        const float distance = pointToSegmentMeters(position.latitude,
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
    _nearestRouteIndex = static_cast<uint16_t>(nearestIndex);

    const float destinationDistance = distanceMeters(position.latitude,
                                                     position.longitude,
                                                     _destination.latitude,
                                                     _destination.longitude);
    if (destinationDistance <= kArrivalDistanceM) {
        _state = RouteState::Arrived;
        _destination.active = false;
        if (_settings != nullptr) _settings->setDestinationActive(false);
        emitNavigation(NavigationState::Arrived, "ARRIVED", &position);
        _hasRoute = false;
        return;
    }

    RouteStep* step = &_routeSteps[_currentStepIndex];
    float distanceToTurn = distanceMeters(position.latitude,
                                          position.longitude,
                                          step->latitude,
                                          step->longitude);
    while (_currentStepIndex + 1 < _routeSteps.size() &&
           (distanceToTurn <= kStepAdvanceDistanceM ||
            _nearestRouteIndex > step->routeIndex + 2)) {
        ++_currentStepIndex;
        step = &_routeSteps[_currentStepIndex];
        distanceToTurn = distanceMeters(position.latitude,
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
        _rerouteRequested = true;
        _state = RouteState::OffRoute;
        _nextRouteAttemptMs = 0;
        emitNavigation(NavigationState::OffRoute,
                       internetConnected ? "OFF ROUTE" : "OFFLINE - WAITING",
                       &position);
        return;
    }

    _state = RouteState::Navigating;
    emitNavigation(NavigationState::Update,
                   internetConnected ? "NAVIGATION" : "NAVIGATION OFFLINE",
                   &position);
}

void RouteManager::emitNavigation(NavigationState navigationState,
                                  const String& status,
                                  const GpsSnapshot* position) {
    NavigationData data;
    data.state = navigationState;
    data.vehicle = _destination.vehicle;
    data.sequence = ++_sequence;
    data.statusMessage = status;
    data.wifiConnected = WiFi.status() == WL_CONNECTED;
    if (position != nullptr) {
        data.gpsValid = position->valid;
        data.satellites = position->satellites;
        data.speedKmh = position->speedKmh;
        data.latitude = position->latitude;
        data.longitude = position->longitude;
        data.courseDeg = position->courseDeg;
    }

    if (_hasRoute && !_routeSteps.empty()) {
        const RouteStep& step = _routeSteps[_currentStepIndex];
        data.maneuver = step.maneuver;
        data.roadName = String(step.roadName);
        if (position != nullptr && position->valid) {
            data.distanceToTurnM = static_cast<uint32_t>(distanceMeters(position->latitude,
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
        data.roadName = _destination.label.length() ? _destination.label : "Waiting for route";
    }

    _currentNavigation = data;
    _lastUpdateMs = millis();
    if (_callback != nullptr) _callback(data);
}

RouteState RouteManager::state() const { return _state; }
const DestinationData& RouteManager::destination() const { return _destination; }
const String& RouteManager::lastError() const { return _lastError; }
bool RouteManager::hasRoute() const { return _hasRoute; }
uint32_t RouteManager::sequence() const { return _sequence; }
uint32_t RouteManager::lastUpdateMillis() const { return _lastUpdateMs; }
const NavigationData& RouteManager::currentNavigation() const { return _currentNavigation; }
const std::vector<RouteManager::RoutePoint>& RouteManager::routePoints() const { return _routePoints; }
uint16_t RouteManager::nearestRouteIndex() const { return _nearestRouteIndex; }
uint32_t RouteManager::routeRevision() const { return _routeRevision; }
