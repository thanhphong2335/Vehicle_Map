#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>

#include "MotoMapTypes.h"
#include "NavMath.h"

class GpsManager;
class SettingsStore;

class RouteManager {
public:
    using RoutePoint = NavMath::RoutePoint;

    struct StatusSnapshot {
        RouteState state = RouteState::Idle;
        DestinationData destination{};
        NavigationData navigation{};
        String lastError;
        uint32_t sequence = 0;
        uint32_t lastUpdateMs = 0;
        bool hasRoute = false;
    };

    RouteManager(GpsManager* gps, SettingsStore* settings);
    ~RouteManager();

    void begin(NavigationUpdateCallback callback);
    void loop(bool internetConnected);

    bool setDestination(const DestinationData& destination, String* error = nullptr);
    void stop();
    void requestReroute();

    RouteState state() const;
    DestinationData destination() const;
    String lastError() const;
    bool hasRoute() const;
    uint32_t sequence() const;
    uint32_t lastUpdateMillis() const;
    NavigationData currentNavigation() const;
    void getStatusSnapshot(StatusSnapshot& outSnapshot) const;

    // Core 1 rendering/pre-fetch accessors (mutated exclusively on Core 1)
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

    struct RouteFetchJob {
        double startLat = 0.0;
        double startLon = 0.0;
        double destLat = 0.0;
        double destLon = 0.0;
        uint32_t generation = 0;
        bool isReroute = false;
    };

    struct PendingRouteResult {
        bool ready = false;
        bool ok = false;
        bool isReroute = false;
        uint32_t generation = 0;
        float distanceM = 0.0F;
        float durationS = 0.0F;
        char error[64]{};
        std::vector<RoutePoint> points;
        std::vector<RouteStep> steps;
        GpsSnapshot startPosition{};
    };

    enum class PendingControl : uint8_t {
        None = 0,
        StartDestination,
        Stop,
        Reroute,
    };

    static constexpr size_t kMaximumRoutePoints = 2048;
    static constexpr size_t kMaximumRouteSteps = 128;

    GpsManager* _gps;
    SettingsStore* _settings;
    NavigationUpdateCallback _callback = nullptr;
    mutable SemaphoreHandle_t _mutex = nullptr;
    QueueHandle_t _routeQueue = nullptr;
    TaskHandle_t _routeTask = nullptr;

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
    uint32_t _generation = 1;
    uint8_t _offRouteFixes = 0;
    bool _hasRoute = false;
    bool _rerouteRequested = false;
    bool _fetchInFlight = false;
    PendingControl _pendingControl = PendingControl::None;
    PendingRouteResult _pendingResult;
    NavigationData _currentNavigation;

    static void routeTaskThunk(void* parameter);
    void routeWorkerLoop();

    bool fetchRouteInternal(const RouteFetchJob& job,
                            std::vector<RoutePoint>& points,
                            std::vector<RouteStep>& steps,
                            float& distanceM,
                            float& durationS,
                            char* outError,
                            size_t outErrorSize) const;
    bool parseRouteResponse(Stream& stream,
                            std::vector<RoutePoint>& points,
                            std::vector<RouteStep>& steps,
                            float& distanceM,
                            float& durationS,
                            String& error) const;
    void matchStepRouteIndexes(std::vector<RouteStep>& steps,
                               const std::vector<RoutePoint>& points) const;
    void applyPendingControlAndResult(const GpsSnapshot& position);
    void updateForPosition(const GpsSnapshot& position, bool internetConnected);
    void emitNavigation(NavigationState navigationState,
                        const String& status,
                        const GpsSnapshot* position = nullptr);
};
