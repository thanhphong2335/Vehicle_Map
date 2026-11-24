#pragma once

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "MotoMapTypes.h"
#include "PinConfig.h"

class GpsManager {
public:
    void begin();
    void loop();

    GpsSnapshot snapshot() const;
    uint32_t fixSequence() const;
    uint32_t charactersProcessed() const;

private:
    HardwareSerial _serial{1};
    mutable TinyGPSPlus _gps;
    mutable SemaphoreHandle_t _mutex = nullptr;
    uint32_t _fixSequence = 0;

    bool hasNeoFixLocked() const;
};
