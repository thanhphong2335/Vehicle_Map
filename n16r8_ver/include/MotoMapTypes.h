#pragma once

#include <Arduino.h>

enum class DeviceMode : uint8_t {
    Idle = 0,
    Navigation = 1,
    Media = 2,
    Error = 3,
};

enum class NavigationState : uint8_t {
    Start,
    Update,
    Stop,
    Arrived,
    OffRoute,
};

enum class Maneuver : uint8_t {
    Straight,
    Left,
    Right,
    UTurn,
    Roundabout,
    Arrive,
};

enum class RouteState : uint8_t {
    Idle,
    WaitingGps,
    Requesting,
    Navigating,
    OffRoute,
    Arrived,
    Error,
};

struct GpsSnapshot {
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    float speedKmh = 0.0F;
    float courseDeg = 0.0F;
    float hdop = 99.9F;
    uint8_t satellites = 0;
    uint32_t ageMs = UINT32_MAX;
};

struct DestinationData {
    bool valid = false;
    bool active = false;
    double latitude = 0.0;
    double longitude = 0.0;
    String label;
    String vehicle = "motorcycle";
};

struct NavigationData {
    NavigationState state = NavigationState::Stop;
    Maneuver maneuver = Maneuver::Straight;
    uint32_t distanceToTurnM = 0;
    uint32_t remainingDistanceM = 0;
    uint32_t etaSeconds = 0;
    String roadName;
    String vehicle = "motorcycle";
    float speedKmh = 0.0F;
    uint8_t satellites = 0;
    bool gpsValid = false;
    bool wifiConnected = false;
    double latitude = 0.0;
    double longitude = 0.0;
    float courseDeg = 0.0F;
    String statusMessage;
    uint32_t sequence = 0;
};

using NavigationUpdateCallback = void (*)(const NavigationData& data);

inline const char* modeName(DeviceMode mode) {
    switch (mode) {
        case DeviceMode::Idle: return "IDLE";
        case DeviceMode::Navigation: return "NAVIGATION";
        case DeviceMode::Media: return "MEDIA";
        case DeviceMode::Error: return "ERROR";
    }
    return "UNKNOWN";
}

inline const char* maneuverName(Maneuver maneuver) {
    switch (maneuver) {
        case Maneuver::Straight: return "STRAIGHT";
        case Maneuver::Left: return "LEFT";
        case Maneuver::Right: return "RIGHT";
        case Maneuver::UTurn: return "UTURN";
        case Maneuver::Roundabout: return "ROUNDABOUT";
        case Maneuver::Arrive: return "ARRIVE";
    }
    return "STRAIGHT";
}

inline const char* routeStateName(RouteState state) {
    switch (state) {
        case RouteState::Idle: return "IDLE";
        case RouteState::WaitingGps: return "WAIT_GPS";
        case RouteState::Requesting: return "ROUTING";
        case RouteState::Navigating: return "NAVIGATION";
        case RouteState::OffRoute: return "OFF_ROUTE";
        case RouteState::Arrived: return "ARRIVED";
        case RouteState::Error: return "ERROR";
    }
    return "UNKNOWN";
}
