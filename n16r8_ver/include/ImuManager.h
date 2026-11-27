#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>

#include "MotoMapTypes.h"
#include "NavMath.h"

class AlertManager;
class GpsManager;

struct ImuData {
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    float temperature = 0.0f;
    float roll = 0.0f;
    float pitch = 0.0f;
    bool valid = false;
    bool fallDetected = false;
    uint32_t fallDurationMs = 0;
};

class ImuManager {
public:
    explicit ImuManager(AlertManager* alertManager = nullptr, GpsManager* gps = nullptr);
    ~ImuManager();

    bool begin();
    bool getData(ImuData& outData) const;
    bool isFallDetected() const;
    bool isSensorConnected() const;
    void clearFallAlert();

    uint8_t detectedAddress() const;
    uint8_t whoAmI() const;
    uint32_t i2cErrorCount() const;

private:
    AlertManager* _alertManager = nullptr;
    GpsManager* _gps = nullptr;

    TaskHandle_t _taskHandle = nullptr;
    mutable SemaphoreHandle_t _mutex = nullptr;

    ImuData _latestData{};
    uint8_t _whoAmI = 0;
    uint8_t _i2cAddr = 0x68;
    bool _useRepeatedStart = true;
    uint8_t _consecutiveErrors = 0;
    std::atomic<bool> _sensorReady{false};
    std::atomic<bool> _fallAlertActive{false};
    std::atomic<uint8_t> _detectedAddr{0};
    std::atomic<uint8_t> _detectedWhoAmI{0};
    std::atomic<uint32_t> _i2cErrorCount{0};

    // Fall detection parameters and thresholds
    static constexpr float kFallAngleDeg = 45.0f;
    static constexpr float kClearAngleDeg = 40.0f;
    static constexpr uint32_t kFallHoldMs = 5000;
    static constexpr uint32_t kClearHoldMs = 2000;
    static constexpr uint8_t kMaxConsecutiveErrors = 5;

    NavMath::ComplementaryAngleFilter _angleFilter{0.98F};
    NavMath::FallDetectorFsm _fallFsm{kFallAngleDeg, kClearAngleDeg, kFallHoldMs, kClearHoldMs};
    uint32_t _lastSampleUs = 0;
    bool _alertDispatched = false;

    static void taskTrampoline(void* param);
    void runTask();
    bool initHardware();
    bool recoverI2cBus();
    bool probeAddress(uint8_t addr, uint8_t& outWhoAmI, bool& outRepeatedStart);
    void evaluateFall(ImuData& current, uint32_t now);

    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t addr, uint8_t reg, uint8_t& outValue, bool repeatedStart);
    bool readBurst(uint8_t reg, uint8_t* buffer, size_t length);
};
