#include "ButtonManager.h"

ButtonManager::ButtonManager(uint8_t pin, uint32_t debounceMs, int activeState)
    : _pin(pin), _debounceMs(debounceMs), _activeState(activeState) {}

void ButtonManager::begin() {
    pinMode(_pin, INPUT_PULLUP);
    _lastReading = digitalRead(_pin);
    _stableState = _lastReading;
    _lastDebounceTime = millis();
    _pressedEvent = false;
    Serial.printf("[BUTTON] Initialized on GPIO %u (current: %s, active: %s)\n",
                  _pin,
                  _lastReading == LOW ? "LOW" : "HIGH",
                  _activeState == LOW ? "LOW" : "HIGH");
}

void ButtonManager::loop() {
    const int reading = digitalRead(_pin);
    const uint32_t now = millis();

    if (reading != _lastReading) {
        _lastDebounceTime = now;
    }

    if ((now - _lastDebounceTime) >= _debounceMs) {
        if (reading != _stableState) {
            _stableState = reading;
            if (_stableState == _activeState) {
                _pressedEvent = true;
                Serial.printf("[BUTTON] GPIO %u PRESSED (%s)\n",
                              _pin,
                              _activeState == HIGH ? "HIGH/OPEN" : "LOW/CLOSED");
            } else {
                Serial.printf("[BUTTON] GPIO %u RELEASED (%s)\n",
                              _pin,
                              _activeState == HIGH ? "LOW/CLOSED" : "HIGH/OPEN");
            }
        }
    }

    _lastReading = reading;
}

bool ButtonManager::wasPressed() {
    if (_pressedEvent) {
        _pressedEvent = false;
        return true;
    }
    return false;
}
