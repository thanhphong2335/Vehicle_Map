#include "NavMath.h"

#include <cfloat>
#include <cmath>

namespace NavMath {

namespace {
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kDegToRad = 0.01745329251994329577;
constexpr double kRadToDeg = 57.2957795130823208768;

double radiansFromDegrees(double degrees) {
    return degrees * kDegToRad;
}

bool decodePolylineValue(const char* encoded,
                         size_t encodedLength,
                         size_t& index,
                         int32_t& value) {
    int32_t result = 0;
    uint8_t shift = 0;
    while (index < encodedLength) {
        const int32_t byteValue = static_cast<int32_t>(static_cast<unsigned char>(encoded[index++])) - 63;
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
} // namespace

float shortestAngleDelta(float fromDegrees, float toDegrees) {
    return std::fmod(toDegrees - fromDegrees + 540.0F, 360.0F) - 180.0F;
}

float bearingDegrees(double fromLatitude,
                     double fromLongitude,
                     double toLatitude,
                     double toLongitude) {
    const double fromLatRad = radiansFromDegrees(fromLatitude);
    const double toLatRad = radiansFromDegrees(toLatitude);
    const double lonDelta = radiansFromDegrees(toLongitude - fromLongitude);
    const double y = std::sin(lonDelta) * std::cos(toLatRad);
    const double x = std::cos(fromLatRad) * std::sin(toLatRad) -
                     std::sin(fromLatRad) * std::cos(toLatRad) * std::cos(lonDelta);
    float result = static_cast<float>(std::atan2(y, x) * kRadToDeg);
    if (result < 0.0F) result += 360.0F;
    return result;
}

float distanceMeters(double lat1, double lon1, double lat2, double lon2) {
    const double dLat = radiansFromDegrees(lat2 - lat1);
    const double dLon = radiansFromDegrees(lon2 - lon1);
    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                     std::cos(radiansFromDegrees(lat1)) * std::cos(radiansFromDegrees(lat2)) *
                     std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    return static_cast<float>(kEarthRadiusM * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a)));
}

float pointToSegmentMeters(double latitude,
                           double longitude,
                           double startLatitude,
                           double startLongitude,
                           double endLatitude,
                           double endLongitude) {
    const double latRad = radiansFromDegrees(latitude);
    const double sx = radiansFromDegrees(startLongitude - longitude) * std::cos(latRad) * kEarthRadiusM;
    const double sy = radiansFromDegrees(startLatitude - latitude) * kEarthRadiusM;
    const double ex = radiansFromDegrees(endLongitude - longitude) * std::cos(latRad) * kEarthRadiusM;
    const double ey = radiansFromDegrees(endLatitude - latitude) * kEarthRadiusM;
    const double dx = ex - sx;
    const double dy = ey - sy;
    const double lengthSquared = dx * dx + dy * dy;
    double projection = lengthSquared > 0.0001 ? -(sx * dx + sy * dy) / lengthSquared : 0.0;
    if (projection < 0.0) projection = 0.0;
    if (projection > 1.0) projection = 1.0;
    const double px = sx + projection * dx;
    const double py = sy + projection * dy;
    return static_cast<float>(std::sqrt(px * px + py * py));
}

bool decodePolyline6(const char* encoded,
                     size_t encodedLength,
                     std::vector<RoutePoint>& points,
                     size_t maxPoints,
                     const char** errorCode) {
    points.clear();
    if (encoded == nullptr || encodedLength == 0 || maxPoints < 2) {
        if (errorCode != nullptr) *errorCode = "route_geometry_missing";
        return false;
    }

    size_t index = 0;
    size_t pointCount = 0;
    int32_t latitude = 0;
    int32_t longitude = 0;
    while (index < encodedLength) {
        int32_t deltaLat = 0;
        int32_t deltaLon = 0;
        if (!decodePolylineValue(encoded, encodedLength, index, deltaLat) ||
            !decodePolylineValue(encoded, encodedLength, index, deltaLon)) {
            if (errorCode != nullptr) *errorCode = "invalid_polyline6";
            return false;
        }
        latitude += deltaLat;
        longitude += deltaLon;
        ++pointCount;
    }
    if (pointCount < 2) {
        if (errorCode != nullptr) *errorCode = "route_geometry_short";
        return false;
    }

    const size_t stride = (pointCount + maxPoints - 1) / maxPoints;
    index = 0;
    latitude = 0;
    longitude = 0;
    size_t rawIndex = 0;
    RoutePoint finalPoint;
    while (index < encodedLength) {
        int32_t deltaLat = 0;
        int32_t deltaLon = 0;
        decodePolylineValue(encoded, encodedLength, index, deltaLat);
        decodePolylineValue(encoded, encodedLength, index, deltaLon);
        latitude += deltaLat;
        longitude += deltaLon;
        finalPoint.latitude = static_cast<float>(latitude / 1000000.0);
        finalPoint.longitude = static_cast<float>(longitude / 1000000.0);
        if (rawIndex % stride == 0 && points.size() < maxPoints) {
            points.push_back(finalPoint);
        }
        ++rawIndex;
    }
    if (points.empty()) {
        if (errorCode != nullptr) *errorCode = "route_geometry_empty";
        return false;
    }
    if (distanceMeters(points.back().latitude,
                       points.back().longitude,
                       finalPoint.latitude,
                       finalPoint.longitude) > 1.0F) {
        if (points.size() == maxPoints) points.back() = finalPoint;
        else points.push_back(finalPoint);
    }
    return points.size() >= 2;
}

ComplementaryAngleFilter::ComplementaryAngleFilter(float alpha)
    : _alpha(alpha < 0.0F ? 0.0F : (alpha > 1.0F ? 1.0F : alpha)) {}

void ComplementaryAngleFilter::reset() {
    _initialized = false;
    _rollDeg = 0.0F;
    _pitchDeg = 0.0F;
}

void ComplementaryAngleFilter::update(float ax,
                                      float ay,
                                      float az,
                                      float gxRad,
                                      float gyRad,
                                      float /*gzRad*/,
                                      float dtSec,
                                      float& outRollDeg,
                                      float& outPitchDeg) {
    const float accelRollDeg = std::atan2(ay, az) * static_cast<float>(kRadToDeg);
    const float accelPitchDeg = std::atan2(-ax, std::hypot(ay, az)) * static_cast<float>(kRadToDeg);

    if (!_initialized || dtSec <= 0.0F || dtSec > 0.25F) {
        _rollDeg = accelRollDeg;
        _pitchDeg = accelPitchDeg;
        _initialized = true;
        outRollDeg = _rollDeg;
        outPitchDeg = _pitchDeg;
        return;
    }

    const float gxDegPerSec = gxRad * static_cast<float>(kRadToDeg);
    const float gyDegPerSec = gyRad * static_cast<float>(kRadToDeg);

    const float predictedRoll = _rollDeg + gxDegPerSec * dtSec;
    const float predictedPitch = _pitchDeg + gyDegPerSec * dtSec;

    // Blend using shortest angular delta to avoid discontinuities around +-180 deg.
    const float rollError = shortestAngleDelta(predictedRoll, accelRollDeg);
    const float pitchError = shortestAngleDelta(predictedPitch, accelPitchDeg);

    _rollDeg = predictedRoll + (1.0F - _alpha) * rollError;
    _pitchDeg = predictedPitch + (1.0F - _alpha) * pitchError;

    // Normalize into (-180, 180]
    _rollDeg = shortestAngleDelta(0.0F, _rollDeg);
    _pitchDeg = shortestAngleDelta(0.0F, _pitchDeg);

    outRollDeg = _rollDeg;
    outPitchDeg = _pitchDeg;
}

FallDetectorFsm::FallDetectorFsm(float fallAngleDeg,
                                 float clearAngleDeg,
                                 uint32_t fallHoldMs,
                                 uint32_t clearHoldMs)
    : _fallAngleDeg(fallAngleDeg)
    , _clearAngleDeg(clearAngleDeg)
    , _fallHoldMs(fallHoldMs)
    , _clearHoldMs(clearHoldMs) {}

void FallDetectorFsm::reset() {
    _state = FallState::Upright;
    _stateStartedMs = 0;
    _hasStartMs = false;
}

void FallDetectorFsm::dismiss() {
    _state = FallState::Dismissed;
    _stateStartedMs = 0;
    _hasStartMs = false;
}

FallStepResult FallDetectorFsm::update(float rollDeg, float pitchDeg, uint32_t nowMs) {
    FallStepResult result{};
    const float absRoll = std::fabs(rollDeg);
    const float absPitch = std::fabs(pitchDeg);
    const bool exceeded = (absRoll > _fallAngleDeg) || (absPitch > _fallAngleDeg);
    const bool upright = (absRoll < _clearAngleDeg) && (absPitch < _clearAngleDeg);

    switch (_state) {
        case FallState::Upright:
            if (exceeded) {
                _state = FallState::Tilting;
                _stateStartedMs = nowMs;
                _hasStartMs = true;
            }
            break;

        case FallState::Tilting:
            if (!exceeded) {
                _state = FallState::Upright;
                _hasStartMs = false;
            } else {
                const uint32_t elapsed = _hasStartMs ? (nowMs - _stateStartedMs) : 0;
                result.tiltDurationMs = elapsed;
                if (elapsed >= _fallHoldMs) {
                    _state = FallState::FallConfirmed;
                    _hasStartMs = false;
                    result.fallActive = true;
                    result.newlyConfirmed = true;
                }
            }
            break;

        case FallState::FallConfirmed:
            result.fallActive = true;
            result.tiltDurationMs = _fallHoldMs;
            if (upright) {
                if (!_hasStartMs) {
                    _stateStartedMs = nowMs;
                    _hasStartMs = true;
                } else if ((nowMs - _stateStartedMs) >= _clearHoldMs) {
                    _state = FallState::Upright;
                    _hasStartMs = false;
                    result.fallActive = false;
                    result.newlyCleared = true;
                }
            } else {
                _hasStartMs = false;
            }
            break;

        case FallState::Dismissed:
            if (upright) {
                if (!_hasStartMs) {
                    _stateStartedMs = nowMs;
                    _hasStartMs = true;
                } else if ((nowMs - _stateStartedMs) >= _clearHoldMs) {
                    _state = FallState::Upright;
                    _hasStartMs = false;
                }
            } else {
                _hasStartMs = false;
            }
            break;
    }

    return result;
}

} // namespace NavMath
