#pragma once

#include <Arduino.h>

#include "MotoMapTypes.h"

class SettingsStore {
public:
    bool begin();

    bool loadWifi(String& ssid, String& password) const;
    bool saveWifi(const String& ssid, const String& password);
    bool hasWifi() const;

    DestinationData loadDestination() const;
    bool saveDestination(const DestinationData& destination);
    bool setDestinationActive(bool active);
};
