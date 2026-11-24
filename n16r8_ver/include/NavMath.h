#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NavMath {

struct RoutePoint {
    float latitude = 0.0F;
    float longitude = 0.0F;
};

float shortestAngleDelta(float fromDegrees, float toDegrees);
float bearingDegrees(double fromLatitude,
                     double fromLongitude,
                     double toLatitude,
                     double toLongitude);
float distanceMeters(double lat1, double lon1, double lat2, double lon2);
float pointToSegmentMeters(double latitude,
                           double longitude,
                           double startLatitude,
                           double startLongitude,
                           double endLatitude,
                           double endLongitude);

bool decodePolyline6(const char* encoded,
                     size_t encodedLength,
                     std::vector<RoutePoint>& points,
                     size_t maxPoints,
                     const char** errorCode = nullptr);

/**
 * 50 Hz complementary filter fusing 3-axis accelerometer tilt with 3-axis gyroscope rates.
 */
class ComplementaryAngleFilter {
public:
    explicit ComplementaryAngleFilter(float alpha = 0.98F);

    void reset();
    void update(float ax,
                float ay,
                float az,
                float gxRad,
                float gyRad,
                float gzRad,
                float dtSec,
                float& outRollDeg,
                float& outPitchDeg);

    bool initialized() const { return _initialized; }

private:
    float _alpha;
    bool _initialized = false;
    float _rollDeg = 0.0F;
    float _pitchDeg = 0.0F;
};

enum class FallState : uint8_t {
    Upright = 0,
    Tilting = 1,
    FallConfirmed = 2,
    Dismissed = 3,
};

struct FallStepResult {
    bool fallActive = false;
    bool newlyConfirmed = false;
    bool newlyCleared = false;
    uint32_t tiltDurationMs = 0;
};

/**
 * Hysteresis FSM for motorcycle fall detection preventing transient bump false positives.
 */
class FallDetectorFsm {
public:
    FallDetectorFsm(float fallAngleDeg = 45.0F,
                    float clearAngleDeg = 40.0F,
                    uint32_t fallHoldMs = 5000,
                    uint32_t clearHoldMs = 2000);

    void reset();
    void dismiss();
    FallStepResult update(float rollDeg, float pitchDeg, uint32_t nowMs);
    FallState state() const { return _state; }

private:
    float _fallAngleDeg;
    float _clearAngleDeg;
    uint32_t _fallHoldMs;
    uint32_t _clearHoldMs;
    FallState _state = FallState::Upright;
    uint32_t _stateStartedMs = 0;
    bool _hasStartMs = false;
};

} // namespace NavMath
