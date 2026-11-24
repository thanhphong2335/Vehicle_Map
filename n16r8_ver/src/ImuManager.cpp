#include "ImuManager.h"

#include <Wire.h>
#include <cmath>
#include <esp_task_wdt.h>

#include "AlertManager.h"
#include "GpsManager.h"
#include "PinConfig.h"

namespace {
constexpr uint8_t MPU_ADDR = 0x68;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr float ACCEL_LSB_PER_G = 4096.0f;
constexpr float GYRO_LSB_PER_DEG = 65.5f;
constexpr float G_TO_MS2 = 9.80665f;
constexpr TickType_t SAMPLE_INTERVAL = pdMS_TO_TICKS(20); // 50 Hz sampling matches DLPF ~20 Hz (Nyquist safe)

constexpr uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 3;
constexpr BaseType_t kTaskCore = 0; // Pinned to Core 0 to prevent UI lag on Core 1
} // namespace

ImuManager::ImuManager(AlertManager* alertManager, GpsManager* gps)
    : _alertManager(alertManager), _gps(gps) {}

ImuManager::~ImuManager() {
    if (_taskHandle != nullptr) {
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
    if (_mutex != nullptr) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool ImuManager::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) {
            Serial.println("[IMU] ERROR: Failed to create mutex!");
            return false;
        }
    }

    const BaseType_t res = xTaskCreatePinnedToCore(
        taskTrampoline,
        "ImuTask",
        kTaskStackSize,
        this,
        kTaskPriority,
        &_taskHandle,
        kTaskCore
    );

    if (res != pdPASS) {
        Serial.println("[IMU] ERROR: Failed to spawn ImuTask!");
        return false;
    }

    Serial.println("[IMU] Background task initialized on Core 0 (50 Hz Complementary Filter).");
    return true;
}

void ImuManager::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t ImuManager::readReg(uint8_t reg) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(static_cast<uint8_t>(MPU_ADDR), static_cast<uint8_t>(1));
    return Wire.available() ? Wire.read() : 0xFF;
}

bool ImuManager::initHardware() {
    Wire.begin(PinConfig::kI2cSdaPin, PinConfig::kI2cSclPin);
    Wire.setClock(100000);

    _whoAmI = readReg(REG_WHO_AM_I);
    Serial.printf("[IMU] I2C SDA: %d, SCL: %d, WHO_AM_I: 0x%02X\n",
                  PinConfig::kI2cSdaPin, PinConfig::kI2cSclPin, _whoAmI);

    if (_whoAmI == 0xFF || _whoAmI == 0x00) {
        Serial.println("[IMU] WARNING: Sensor not detected or invalid address.");
        _sensorReady.store(false, std::memory_order_release);
        return false;
    }

    // Wake up MPU (use internal 8MHz clock)
    writeReg(REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure Accel to +-8g
    writeReg(REG_ACCEL_CONFIG, 0x10);
    // Configure Gyro to +-500 deg/s
    writeReg(REG_GYRO_CONFIG, 0x08);
    // Low pass filter DLPF ~20Hz
    writeReg(REG_CONFIG, 0x04);

    _angleFilter.reset();
    _lastSampleUs = micros();
    _sensorReady.store(true, std::memory_order_release);
    Serial.println("[IMU] Sensor hardware initialized successfully.");
    return true;
}

void ImuManager::taskTrampoline(void* param) {
    auto* self = static_cast<ImuManager*>(param);
    self->runTask();
}

void ImuManager::runTask() {
    esp_task_wdt_add(nullptr);
    initHardware();

    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t lastRetryAttemptMs = 0;

    for (;;) {
        esp_task_wdt_reset();
        const uint32_t now = millis();

        if (!_sensorReady.load(std::memory_order_acquire)) {
            if (now - lastRetryAttemptMs >= 3000) {
                lastRetryAttemptMs = now;
                initHardware();
            }
            vTaskDelayUntil(&lastWakeTime, SAMPLE_INTERVAL);
            continue;
        }

        Wire.beginTransmission(MPU_ADDR);
        Wire.write(REG_ACCEL_XOUT_H);
        const uint8_t err = Wire.endTransmission(false);

        if (err != 0 || Wire.requestFrom(static_cast<uint8_t>(MPU_ADDR), static_cast<uint8_t>(14)) != 14) {
            Serial.println("[IMU] Read error on I2C bus.");
            _sensorReady.store(false, std::memory_order_release);
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                _latestData.valid = false;
                xSemaphoreGive(_mutex);
            }
            vTaskDelayUntil(&lastWakeTime, SAMPLE_INTERVAL);
            continue;
        }

        int16_t raw[7];
        for (int i = 0; i < 7; ++i) {
            raw[i] = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
        }

        ImuData data{};
        data.ax = (static_cast<float>(raw[0]) / ACCEL_LSB_PER_G) * G_TO_MS2;
        data.ay = (static_cast<float>(raw[1]) / ACCEL_LSB_PER_G) * G_TO_MS2;
        data.az = (static_cast<float>(raw[2]) / ACCEL_LSB_PER_G) * G_TO_MS2;

        data.temperature = (_whoAmI == 0x68)
            ? (static_cast<float>(raw[3]) / 340.0f + 36.53f)
            : (static_cast<float>(raw[3]) / 333.87f + 21.0f);

        data.gx = (static_cast<float>(raw[4]) / GYRO_LSB_PER_DEG) * DEG_TO_RAD;
        data.gy = (static_cast<float>(raw[5]) / GYRO_LSB_PER_DEG) * DEG_TO_RAD;
        data.gz = (static_cast<float>(raw[6]) / GYRO_LSB_PER_DEG) * DEG_TO_RAD;

        const uint32_t nowUs = micros();
        const float dtSec = (_lastSampleUs == 0)
            ? 0.02F
            : static_cast<float>(nowUs - _lastSampleUs) / 1000000.0F;
        _lastSampleUs = nowUs;

        _angleFilter.update(data.ax, data.ay, data.az, data.gx, data.gy, data.gz, dtSec, data.roll, data.pitch);
        data.valid = true;

        // Evaluate fall conditions under mutex
        evaluateFall(data, now);

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            _latestData = data;
            xSemaphoreGive(_mutex);
        }

        vTaskDelayUntil(&lastWakeTime, SAMPLE_INTERVAL);
    }
}

void ImuManager::evaluateFall(ImuData& current, uint32_t now) {
    bool shouldDispatch = false;
    {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            return;
        }
        const NavMath::FallStepResult step = _fallFsm.update(current.roll, current.pitch, now);
        current.fallDetected = step.fallActive;
        current.fallDurationMs = step.tiltDurationMs;
        _fallAlertActive.store(step.fallActive, std::memory_order_release);

        if (step.newlyConfirmed && !_alertDispatched) {
            _alertDispatched = true;
            shouldDispatch = true;
            Serial.printf("[IMU] FALL DETECTED! Roll: %.1f deg, Pitch: %.1f deg\n", current.roll, current.pitch);
        } else if (step.newlyCleared || _fallFsm.state() == NavMath::FallState::Upright) {
            if (step.newlyCleared) {
                Serial.println("[IMU] Vehicle righted. Fall alert cleared.");
            }
            _alertDispatched = false;
        }
        xSemaphoreGive(_mutex);
    }

    if (shouldDispatch && _alertManager != nullptr) {
        AlertEvent event{};
        snprintf(event.event, sizeof(event.event), "Fall Detected (Roll: %.0f deg)", current.roll);

        if (_gps != nullptr) {
            const GpsSnapshot gps = _gps->snapshot(); // Thread-safe GPS snapshot
            if (gps.valid) {
                event.latitude = gps.latitude;
                event.longitude = gps.longitude;
                event.speedKmh = gps.speedKmh;
                event.courseDeg = gps.courseDeg;
            }
        }
        snprintf(event.status, sizeof(event.status), "Auto-Triggered by IMU");
        _alertManager->triggerAlert(event);
    }
}

bool ImuManager::getData(ImuData& outData) const {
    if (_mutex == nullptr) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        outData = _latestData;
        xSemaphoreGive(_mutex);
        return true;
    }
    return false;
}

bool ImuManager::isFallDetected() const {
    return _fallAlertActive.load(std::memory_order_acquire);
}

bool ImuManager::isSensorConnected() const {
    return _sensorReady.load(std::memory_order_acquire);
}

void ImuManager::clearFallAlert() {
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _fallFsm.dismiss();
        _fallAlertActive.store(false, std::memory_order_release);
        _latestData.fallDetected = false;
        _latestData.fallDurationMs = 0;
        xSemaphoreGive(_mutex);
    } else {
        _fallAlertActive.store(false, std::memory_order_release);
    }
    Serial.println("[IMU] User manually cleared fall alert.");
}
