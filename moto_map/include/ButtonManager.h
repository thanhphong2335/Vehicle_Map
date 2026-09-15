#pragma once

#include <Arduino.h>

class ButtonManager {
public:
    explicit ButtonManager(uint8_t pin = 32, uint32_t debounceMs = 25, int activeState = LOW);

    void begin();
    void loop();
    bool wasPressed();

private:
    uint8_t _pin;
    uint32_t _debounceMs;
    int _activeState = LOW;
    int _lastReading = HIGH;
    int _stableState = HIGH;
    uint32_t _lastDebounceTime = 0;
    bool _pressedEvent = false;
};
