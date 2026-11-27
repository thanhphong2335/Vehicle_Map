#include "ImuManager.h"

#include <Wire.h>
#include <cmath>
#include <esp_task_wdt.h>

#include "AlertManager.h"
#include "GpsManager.h"
#include "PinConfig.h"

namespace {
constexpr uint8_t kMpuAddresses[] = {0x68, 0x69};
constexpr uint8_t REG_SMPLRT_DIV = 0x19;
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

constexpr uint32_t kTaskStackSize = 6144;
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

bool ImuManager::recoverI2cBus() {
    Wire.end();

    // Bit-bang up to 9 SCL pulses if an I2C slave is holding SDA LOW after a reset/glitch.
    pinMode(PinConfig::kI2cSdaPin, INPUT_PULLUP);
    pinMode(PinConfig::kI2cSclPin, INPUT_PULLUP);
    delayMicroseconds(10);

    if (digitalRead(PinConfig::kI2cSdaPin) == LOW) {
        pinMode(PinConfig::kI2cSclPin, OUTPUT);
        for (int i = 0; i < 9 && digitalRead(PinConfig::kI2cSdaPin) == LOW; ++i) {
            digitalWrite(PinConfig::kI2cSclPin, LOW);
            delayMicroseconds(10);
            digitalWrite(PinConfig::kI2cSclPin, HIGH);
            delayMicroseconds(10);
        }
        // Generate STOP condition: SDA LOW -> SCL HIGH -> SDA HIGH
        pinMode(PinConfig::kI2cSdaPin, OUTPUT);
        digitalWrite(PinConfig::kI2cSdaPin, LOW);
        delayMicroseconds(10);
        digitalWrite(PinConfig::kI2cSclPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(PinConfig::kI2cSdaPin, HIGH);
        delayMicroseconds(10);
        pinMode(PinConfig::kI2cSdaPin, INPUT_PULLUP);
        pinMode(PinConfig::kI2cSclPin, INPUT_PULLUP);
    }

    const bool ok = Wire.begin(PinConfig::kI2cSdaPin, PinConfig::kI2cSclPin, 100000);
    Wire.setTimeOut(50);
    return ok;
}

bool ImuManager::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_i2cAddr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission(true) == 0;
}

bool ImuManager::readReg(uint8_t addr, uint8_t reg, uint8_t& outValue, bool repeatedStart) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    const uint8_t err = Wire.endTransmission(!repeatedStart);
    if (err != 0) {
        return false;
    }
    if (Wire.requestFrom(static_cast<uint16_t>(addr), static_cast<uint8_t>(1), true) != 1 ||
        !Wire.available()) {
        return false;
    }
    outValue = static_cast<uint8_t>(Wire.read());
    return true;
}

bool ImuManager::probeAddress(uint8_t addr, uint8_t& outWhoAmI, bool& outRepeatedStart) {
    uint8_t who = 0xFF;
    if (readReg(addr, REG_WHO_AM_I, who, true) && who != 0xFF && who != 0x00) {
        outWhoAmI = who;
        outRepeatedStart = true;
        return true;
    }
    if (readReg(addr, REG_WHO_AM_I, who, false) && who != 0xFF && who != 0x00) {
        outWhoAmI = who;
        outRepeatedStart = false;
        return true;
    }
    return false;
}

bool ImuManager::readBurst(uint8_t reg, uint8_t* buffer, size_t length) {
    auto tryRead = [&](bool repeatedStart) -> bool {
        Wire.beginTransmission(_i2cAddr);
        Wire.write(reg);
        if (Wire.endTransmission(!repeatedStart) != 0) {
            return false;
        }
        if (Wire.requestFrom(static_cast<uint16_t>(_i2cAddr), static_cast<uint8_t>(length), true) != length) {
            return false;
        }
        for (size_t i = 0; i < length; ++i) {
            if (!Wire.available()) {
                return false;
            }
            buffer[i] = static_cast<uint8_t>(Wire.read());
        }
        return true;
    };

    if (tryRead(_useRepeatedStart)) {
        return true;
    }
    if (tryRead(!_useRepeatedStart)) {
        _useRepeatedStart = !_useRepeatedStart;
        return true;
    }
    return false;
}

bool ImuManager::initHardware() {
    if (!recoverI2cBus()) {
        Serial.printf("[IMU] ERROR: Wire.begin failed on SDA=%d SCL=%d\n",
                      PinConfig::kI2cSdaPin, PinConfig::kI2cSclPin);
        _sensorReady.store(false, std::memory_order_release);
        return false;
    }

    uint8_t foundAddr = 0;
    uint8_t foundWho = 0;
    bool repeatedStart = true;
    for (uint8_t addr : kMpuAddresses) {
        if (probeAddress(addr, foundWho, repeatedStart)) {
            foundAddr = addr;
            break;
        }
    }

    if (foundAddr == 0) {
        Serial.printf("[IMU] WARNING: Sensor not detected on I2C (SDA=%d, SCL=%d, tried 0x68/0x69).\n",
                      PinConfig::kI2cSdaPin, PinConfig::kI2cSclPin);
        _whoAmI = 0;
        _detectedAddr.store(0, std::memory_order_release);
        _detectedWhoAmI.store(0, std::memory_order_release);
        _sensorReady.store(false, std::memory_order_release);
        return false;
    }

    _i2cAddr = foundAddr;
    _whoAmI = foundWho;
    _useRepeatedStart = repeatedStart;
    _detectedAddr.store(_i2cAddr, std::memory_order_release);
    _detectedWhoAmI.store(_whoAmI, std::memory_order_release);

    // Wake up MPU (use internal 8MHz clock)
    if (!writeReg(REG_PWR_MGMT_1, 0x00)) {
        Serial.printf("[IMU] ERROR: Failed to wake sensor at 0x%02X.\n", _i2cAddr);
        _sensorReady.store(false, std::memory_order_release);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // Sample rate = 1kHz / (1 + 19) = 50 Hz (matches 20ms SAMPLE_INTERVAL with DLPF enabled)
    bool cfgOk = writeReg(REG_SMPLRT_DIV, 19);
    // Low pass filter DLPF ~20Hz
    cfgOk = cfgOk && writeReg(REG_CONFIG, 0x04);
    // Configure Gyro to +-500 deg/s
    cfgOk = cfgOk && writeReg(REG_GYRO_CONFIG, 0x08);
    // Configure Accel to +-8g
    cfgOk = cfgOk && writeReg(REG_ACCEL_CONFIG, 0x10);

    if (!cfgOk) {
        Serial.printf("[IMU] ERROR: Failed to configure sensor registers at 0x%02X.\n", _i2cAddr);
        _sensorReady.store(false, std::memory_order_release);
        return false;
    }

    _angleFilter.reset();
    _lastSampleUs = micros();
    _consecutiveErrors = 0;
    _sensorReady.store(true, std::memory_order_release);
    Serial.printf("[IMU] Sensor initialized at 0x%02X (WHO_AM_I=0x%02X, mode=%s).\n",
                  _i2cAddr, _whoAmI, _useRepeatedStart ? "rep-start" : "stop-start");
    return true;
}

void ImuManager::taskTrampoline(void* param) {
    auto* self = static_cast<ImuManager*>(param);
    self->runTask();
}

void ImuManager::runTask() {
    esp_task_wdt_add(nullptr);
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow power rail & MPU-6050 startup stabilization
    initHardware();

    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t lastRetryAttemptMs = millis();

    for (;;) {
        esp_task_wdt_reset();
        const uint32_t now = millis();

        if (!_sensorReady.load(std::memory_order_acquire)) {
            if (now - lastRetryAttemptMs >= 3000) {
                lastRetryAttemptMs = now;
                initHardware();
                lastWakeTime = xTaskGetTickCount();
            }
            vTaskDelayUntil(&lastWakeTime, SAMPLE_INTERVAL);
            continue;
        }

        uint8_t rawBytes[14]{};
        if (!readBurst(REG_ACCEL_XOUT_H, rawBytes, sizeof(rawBytes))) {
            _i2cErrorCount.fetch_add(1, std::memory_order_relaxed);
            if (_consecutiveErrors < 255) {
                ++_consecutiveErrors;
            }
            if (_consecutiveErrors >= kMaxConsecutiveErrors) {
                Serial.println("[IMU] Read error on I2C bus (threshold reached, scheduling recovery).");
                _sensorReady.store(false, std::memory_order_release);
                lastRetryAttemptMs = now;
                if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    _latestData.valid = false;
                    xSemaphoreGive(_mutex);
                }
            }
            lastWakeTime = xTaskGetTickCount();
            vTaskDelayUntil(&lastWakeTime, SAMPLE_INTERVAL);
            continue;
        }

        _consecutiveErrors = 0;
        int16_t raw[7];
        for (int i = 0; i < 7; ++i) {
            raw[i] = static_cast<int16_t>((static_cast<uint16_t>(rawBytes[i * 2]) << 8) | rawBytes[i * 2 + 1]);
        }

        ImuData data{};
        data.ax = (static_cast<float>(raw[0]) / ACCEL_LSB_PER_G) * G_TO_MS2;
        data.ay = (static_cast<float>(raw[1]) / ACCEL_LSB_PER_G) * G_TO_MS2;
        data.az = (static_cast<float>(raw[2]) / ACCEL_LSB_PER_G) * G_TO_MS2;

        data.temperature = (_whoAmI == 0x68 || _whoAmI == 0x69 || _whoAmI == 0x72)
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

uint8_t ImuManager::detectedAddress() const {
    return _detectedAddr.load(std::memory_order_acquire);
}

uint8_t ImuManager::whoAmI() const {
    return _detectedWhoAmI.load(std::memory_order_acquire);
}

uint32_t ImuManager::i2cErrorCount() const {
    return _i2cErrorCount.load(std::memory_order_acquire);
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
