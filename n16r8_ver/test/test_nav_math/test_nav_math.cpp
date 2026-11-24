#include <unity.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "NavMath.h"

void setUp(void) {}
void tearDown(void) {}

void test_shortest_angle_delta_wraps_correctly(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 20.0F, NavMath::shortestAngleDelta(350.0F, 10.0F));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, -20.0F, NavMath::shortestAngleDelta(10.0F, 350.0F));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, NavMath::shortestAngleDelta(180.0F, -180.0F));
}

void test_bearing_and_distance_reasonable(void) {
    // Ho Chi Minh City Hall -> Ben Thanh Market (~500-700m SW)
    const float dist = NavMath::distanceMeters(10.776889, 106.700806, 10.772109, 106.698271);
    TEST_ASSERT_TRUE(dist > 400.0F && dist < 800.0F);

    const float bearingNorth = NavMath::bearingDegrees(10.0, 106.0, 10.01, 106.0);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 0.0F, bearingNorth);

    const float bearingEast = NavMath::bearingDegrees(10.0, 106.0, 10.0, 106.01);
    TEST_ASSERT_FLOAT_WITHIN(1.0F, 90.0F, bearingEast);
}

void test_point_to_segment_distance(void) {
    // Point directly on midpoint of segment
    const float onLine = NavMath::pointToSegmentMeters(10.005, 106.0, 10.0, 106.0, 10.01, 106.0);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 0.0F, onLine);

    // Point ~111m east of north-south segment
    const float offLine = NavMath::pointToSegmentMeters(10.005, 106.001, 10.0, 106.0, 10.01, 106.0);
    TEST_ASSERT_TRUE(offLine > 95.0F && offLine < 120.0F);
}

void test_decode_polyline6_valid_and_invalid(void) {
    // Encode two points at 1e-6: (0.000000, 0.000000) -> (0.000010, 0.000020)
    // deltaLat=+10 -> zigzag 20 -> 20+63 = 83 ('S'), deltaLon=+20 -> zigzag 40 -> 40+63 = 103 ('g')
    // Start point: lat=0 ('?'), lon=0 ('?') -> "?_S_g"? Let's encode exact valid polyline6:
    // Point 0: (10, 10) -> 20->83('S'), 20->83('S')
    // Point 1: delta (+500, +500) -> zigzag 1000 = (31 << 5) | 8 -> (8|0x20)+63=103('g'), 31+63=94('^')
    const char validPoly[] = "SSg^g^";
    std::vector<NavMath::RoutePoint> pts;
    const char* err = nullptr;
    const bool ok = NavMath::decodePolyline6(validPoly, strlen(validPoly), pts, 2048, &err);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(pts.size()));
    TEST_ASSERT_FLOAT_WITHIN(1e-5F, 0.000010F, pts[0].latitude);
    TEST_ASSERT_FLOAT_WITHIN(1e-5F, 0.000510F, pts[1].latitude);

    const char* emptyErr = nullptr;
    TEST_ASSERT_FALSE(NavMath::decodePolyline6("", 0, pts, 2048, &emptyErr));
    TEST_ASSERT_EQUAL_STRING("route_geometry_missing", emptyErr);

    const char singlePt[] = "SS";
    const char* shortErr = nullptr;
    TEST_ASSERT_FALSE(NavMath::decodePolyline6(singlePt, strlen(singlePt), pts, 2048, &shortErr));
    TEST_ASSERT_EQUAL_STRING("route_geometry_short", shortErr);
}

void test_complementary_filter_rejects_short_accel_spike(void) {
    NavMath::ComplementaryAngleFilter filter(0.98F);
    float roll = 0.0F;
    float pitch = 0.0F;

    // Step 1: upright static (az = 9.81) -> initializes near 0 deg
    filter.update(0.0F, 0.0F, 9.81F, 0.0F, 0.0F, 0.0F, 0.02F, roll, pitch);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 0.0F, roll);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 0.0F, pitch);

    // Step 2: pothole/braking lateral acceleration spike with zero gyro rate
    // Pure accel atan2(9.81, 9.81) would jump to 45 deg, filter with alpha=0.98 stays ~0.9 deg!
    filter.update(0.0F, 9.81F, 9.81F, 0.0F, 0.0F, 0.0F, 0.02F, roll, pitch);
    TEST_ASSERT_TRUE(std::fabs(roll) < 2.0F);
}

void test_fall_detector_fsm_transition_coverage(void) {
    NavMath::FallDetectorFsm fsm(45.0F, 40.0F, 5000, 2000);

    // Transient tilt (<5s) returns to Upright without SOS
    auto r = fsm.update(50.0F, 0.0F, 1000);
    TEST_ASSERT_EQUAL(NavMath::FallState::Tilting, fsm.state());
    TEST_ASSERT_FALSE(r.fallActive);

    r = fsm.update(10.0F, 0.0F, 3000);
    TEST_ASSERT_EQUAL(NavMath::FallState::Upright, fsm.state());
    TEST_ASSERT_FALSE(r.fallActive);

    // Sustained tilt >= 5000ms confirms fall
    fsm.update(60.0F, 0.0F, 10000);
    r = fsm.update(60.0F, 0.0F, 14999);
    TEST_ASSERT_FALSE(r.newlyConfirmed);
    r = fsm.update(60.0F, 0.0F, 15000);
    TEST_ASSERT_TRUE(r.newlyConfirmed);
    TEST_ASSERT_TRUE(r.fallActive);
    TEST_ASSERT_EQUAL(NavMath::FallState::FallConfirmed, fsm.state());

    // Upright for 2000ms clears confirmed alert automatically
    r = fsm.update(5.0F, 0.0F, 16000);
    TEST_ASSERT_TRUE(r.fallActive); // Not cleared yet (<2000ms)
    r = fsm.update(5.0F, 0.0F, 18000);
    TEST_ASSERT_TRUE(r.newlyCleared);
    TEST_ASSERT_FALSE(r.fallActive);
    TEST_ASSERT_EQUAL(NavMath::FallState::Upright, fsm.state());

    // Dismiss resets only after upright hold
    fsm.update(60.0F, 0.0F, 20000);
    fsm.update(60.0F, 0.0F, 25000);
    fsm.dismiss();
    TEST_ASSERT_EQUAL(NavMath::FallState::Dismissed, fsm.state());
    fsm.update(10.0F, 0.0F, 26000);
    fsm.update(10.0F, 0.0F, 28000);
    TEST_ASSERT_EQUAL(NavMath::FallState::Upright, fsm.state());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_shortest_angle_delta_wraps_correctly);
    RUN_TEST(test_bearing_and_distance_reasonable);
    RUN_TEST(test_point_to_segment_distance);
    RUN_TEST(test_decode_polyline6_valid_and_invalid);
    RUN_TEST(test_complementary_filter_rejects_short_accel_spike);
    RUN_TEST(test_fall_detector_fsm_transition_coverage);
    return UNITY_END();
}
