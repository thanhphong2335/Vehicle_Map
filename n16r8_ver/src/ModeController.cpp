#include "ModeController.h"

#include "DisplayManager.h"

ModeController::ModeController(DisplayManager* display) : _display(display) {}

void ModeController::begin() {
    _mode = DeviceMode::Idle;
    if (_display != nullptr) {
        _display->showIdle();
    }
}

void ModeController::onNavigation(const NavigationData& data, bool render) {
    if (data.state == NavigationState::Stop || data.state == NavigationState::Arrived) {
        _mode = DeviceMode::Idle;
        if (_display != nullptr) {
            _display->showIdle(data.state == NavigationState::Arrived ? "Arrived" : "Navigation stopped");
        }
        return;
    }

    _mode = DeviceMode::Navigation;
    if (_display != nullptr && render) {
        _display->showNavigation(data);
    }
}

void ModeController::onMedia(bool showPlaceholder) {
    _mode = DeviceMode::Media;
    if (_display != nullptr && showPlaceholder) {
        _display->showMediaPlaceholder();
    }
}

void ModeController::onIdle(const String& detail) {
    _mode = DeviceMode::Idle;
    if (_display != nullptr) {
        _display->showIdle(detail);
    }
}

DeviceMode ModeController::mode() const {
    return _mode;
}
