#pragma once

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "MotoMapTypes.h"

class GpsManager {
public:
    void begin();
    void loop();

    GpsSnapshot snapshot();
    uint32_t fixSequence() const;
    uint32_t charactersProcessed() const;

private:
    HardwareSerial _serial{2};
    TinyGPSPlus _gps;
    uint32_t _fixSequence = 0;

    bool hasNeoFix();
};
