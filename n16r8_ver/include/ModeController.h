#pragma once

#include <Arduino.h>
#include <atomic>

#include "MotoMapTypes.h"

class DisplayManager;

class ModeController {
public:
    explicit ModeController(DisplayManager* display);

    void begin();
    void onNavigation(const NavigationData& data, bool render = true);
    void onMedia(bool showPlaceholder = true);
    void onIdle(const String& detail = "Waiting for iPhone");
    DeviceMode mode() const;

private:
    DisplayManager* _display;
    std::atomic<DeviceMode> _mode{DeviceMode::Idle};
};
