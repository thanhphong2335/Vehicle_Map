#pragma once

#include <Arduino.h>
#include <vector>

#include "MotoMapTypes.h"

class GpsManager;
class SettingsStore;

class RouteManager {
public:
    struct RoutePoint {
        float latitude = 0.0F;
        float longitude = 0.0F;
    };

    RouteManager(GpsManager* gps, SettingsStore* settings);

    void begin(NavigationUpdateCallback callback);
    void loop(bool internetConnected);

    bool setDestination(const DestinationData& destination, String* error = nullptr);
    void stop();
    void requestReroute();

    RouteState state() const;
    const DestinationData& destination() const;
    const String& lastError() const;
    bool hasRoute() const;
    uint32_t sequence() const;
    uint32_t lastUpdateMillis() const;
    const NavigationData& currentNavigation() const;
    const std::vector<RoutePoint>& routePoints() const;
    uint16_t nearestRouteIndex() const;
    uint32_t routeRevision() const;

private:
    struct RouteStep {
        float latitude = 0.0F;
        float longitude = 0.0F;
        float distanceM = 0.0F;
        float durationS = 0.0F;
        uint16_t routeIndex = 0;
        Maneuver maneuver = Maneuver::Straight;
        bool depart = false;
        bool arrive = false;
        char roadName[49]{};
    };

    static constexpr size_t kMaximumRoutePoints = 1024;
    static constexpr size_t kMaximumRouteSteps = 96;

    GpsManager* _gps;
    SettingsStore* _settings;
    NavigationUpdateCallback _callback = nullptr;
    DestinationData _destination;
    RouteState _state = RouteState::Idle;
    std::vector<RoutePoint> _routePoints;
    std::vector<RouteStep> _routeSteps;
    size_t _currentStepIndex = 0;
    uint16_t _nearestRouteIndex = 0;
    float _routeDistanceM = 0.0F;
    float _routeDurationS = 0.0F;
    String _lastError;
    uint32_t _sequence = 0;
    uint32_t _lastUpdateMs = 0;
    uint32_t _lastFixSequence = 0;
    uint32_t _lastRerouteMs = 0;
    uint32_t _nextRouteAttemptMs = 0;
    uint32_t _lastWaitingDisplayMs = 0;
    uint32_t _routeRevision = 0;
    uint8_t _offRouteFixes = 0;
    bool _hasRoute = false;
    bool _rerouteRequested = false;
    NavigationData _currentNavigation;

    bool fetchRoute(const GpsSnapshot& position, String& error);
    bool parseRouteResponse(Stream& stream,
                            std::vector<RoutePoint>& points,
                            std::vector<RouteStep>& steps,
                            float& distanceM,
                            float& durationS,
                            String& error);
    bool decodePolyline6(const String& encoded,
                         std::vector<RoutePoint>& points,
                         String& error) const;
    void matchStepRouteIndexes(std::vector<RouteStep>& steps,
                               const std::vector<RoutePoint>& points) const;
    void updateForPosition(const GpsSnapshot& position, bool internetConnected);
    void emitNavigation(NavigationState navigationState,
                        const String& status,
                        const GpsSnapshot* position = nullptr);
};
