#include "ModeController.h"

#include "DisplayManager.h"

ModeController::ModeController(DisplayManager* display) : _display(display) {}

void ModeController::begin() {
    _mode.store(DeviceMode::Idle, std::memory_order_release);
    if (_display != nullptr) {
        _display->showIdle();
    }
}

void ModeController::onNavigation(const NavigationData& data, bool render) {
    if (data.state == NavigationState::Stop || data.state == NavigationState::Arrived) {
        _mode.store(DeviceMode::Idle, std::memory_order_release);
        if (_display != nullptr) {
            _display->showIdle(data.state == NavigationState::Arrived ? "Arrived" : "Navigation stopped");
        }
        return;
    }

    _mode.store(DeviceMode::Navigation, std::memory_order_release);
    if (_display != nullptr) {
        if (render) {
            _display->showNavigation(data);
        } else {
            _display->enterNavigationMode();
        }
    }
}

void ModeController::onMedia(bool showPlaceholder) {
    _mode.store(DeviceMode::Media, std::memory_order_release);
    if (_display != nullptr && showPlaceholder) {
        _display->showMediaPlaceholder();
    }
}

void ModeController::onIdle(const String& detail) {
    _mode.store(DeviceMode::Idle, std::memory_order_release);
    if (_display != nullptr) {
        _display->showIdle(detail);
    }
}

DeviceMode ModeController::mode() const {
    return _mode.load(std::memory_order_acquire);
}
