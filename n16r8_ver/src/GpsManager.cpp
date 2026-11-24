#include "GpsManager.h"

namespace {
constexpr int kGpsRxPin = PinConfig::kGpsRxPin;  // GPS TX -> ESP32-S3 RX
constexpr int kGpsTxPin = PinConfig::kGpsTxPin;  // Optional: ESP32-S3 TX -> GPS RX
constexpr uint32_t kGpsBaud = 9600;
constexpr uint32_t kMaximumFixAgeMs = 3000;
constexpr uint8_t kMinimumSatellites = 4;
}

void GpsManager::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    _serial.begin(kGpsBaud, SERIAL_8N1, kGpsRxPin, kGpsTxPin);
    Serial.printf("GPS UART: RX=%d TX=%d baud=%lu\n",
                  kGpsRxPin,
                  kGpsTxPin,
                  static_cast<unsigned long>(kGpsBaud));
}

void GpsManager::loop() {
    while (_serial.available() > 0) {
        char batch[64];
        size_t count = 0;
        while (_serial.available() > 0 && count < sizeof(batch)) {
            batch[count++] = static_cast<char>(_serial.read());
        }
        if (count == 0) {
            break;
        }

        if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (size_t i = 0; i < count; ++i) {
                if (_gps.encode(batch[i]) && _gps.location.isUpdated()) {
                    ++_fixSequence;
                }
            }
            if (_mutex != nullptr) {
                xSemaphoreGive(_mutex);
            }
        } else {
            break;
        }
    }
}

GpsSnapshot GpsManager::snapshot() const {
    GpsSnapshot result;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return result;
    }
    const uint32_t satellites = _gps.satellites.isValid() ? _gps.satellites.value() : 0;
    result.satellites = static_cast<uint8_t>(satellites > 255 ? 255 : satellites);
    result.ageMs = _gps.location.isValid() ? _gps.location.age() : UINT32_MAX;
    result.valid = hasNeoFixLocked();
    if (_gps.location.isValid()) {
        result.latitude = _gps.location.lat();
        result.longitude = _gps.location.lng();
    }
    result.speedKmh = _gps.speed.isValid() ? _gps.speed.kmph() : 0.0F;
    result.courseDeg = _gps.course.isValid() ? _gps.course.deg() : 0.0F;
    result.hdop = _gps.hdop.isValid() ? _gps.hdop.hdop() : 99.9F;
    if (_mutex != nullptr) {
        xSemaphoreGive(_mutex);
    }
    return result;
}

uint32_t GpsManager::fixSequence() const {
    uint32_t seq = 0;
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        seq = _fixSequence;
        if (_mutex != nullptr) xSemaphoreGive(_mutex);
    }
    return seq;
}

uint32_t GpsManager::charactersProcessed() const {
    uint32_t chars = 0;
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        chars = _gps.charsProcessed();
        if (_mutex != nullptr) xSemaphoreGive(_mutex);
    }
    return chars;
}

bool GpsManager::hasNeoFixLocked() const {
    if (!_gps.location.isValid()) return false;
    const uint32_t satellites = _gps.satellites.isValid() ? _gps.satellites.value() : 0;
    return _gps.location.age() <= kMaximumFixAgeMs && satellites >= kMinimumSatellites;
}
