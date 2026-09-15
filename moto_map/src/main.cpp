#include <Arduino.h>

#include "ButtonManager.h"
#include "ConnectionManager.h"
#include "DisplayManager.h"
#include "GpsManager.h"
#include "MediaManager.h"
#include "ModeController.h"
#if defined(MOTOMAP_NAV_MAP)
#include "NavigationMapManager.h"
#endif
#include "RouteManager.h"
#include "SettingsStore.h"

ButtonManager buttonManager(32);
DisplayManager displayManager;
SettingsStore settingsStore;
GpsManager gpsManager;
MediaManager mediaManager(&displayManager);
ModeController modeController(&displayManager);
RouteManager routeManager(&gpsManager, &settingsStore);
#if defined(MOTOMAP_NAV_MAP)
NavigationMapManager navigationMapManager(&displayManager, &routeManager);
#endif
ConnectionManager connectionManager(
    &displayManager,
    &mediaManager,
    &modeController,
    &gpsManager,
    &routeManager,
    &settingsStore);

void onNavigationUpdate(const NavigationData& data) {
#if defined(MOTOMAP_NAV_MAP)
    navigationMapManager.onNavigation(data);
    const bool renderImmediately = data.state == NavigationState::Stop ||
                                   data.state == NavigationState::Arrived;
    modeController.onNavigation(data, renderImmediately);
#else
    modeController.onNavigation(data);
#endif
}

void setup() {
    Serial.begin(115200);
    delay(200);
    setCpuFrequencyMhz(160);
    Serial.println("\nMotoMap ESP32 starting (CPU: 160MHz)");

    buttonManager.begin();
    displayManager.begin();
    settingsStore.begin();
    gpsManager.begin();
    mediaManager.begin();
#if defined(MOTOMAP_NAV_MAP)
    navigationMapManager.begin();
#endif
    modeController.begin();
    routeManager.begin(onNavigationUpdate);
    connectionManager.begin();

    if (mediaManager.hasSavedImage()) {
        mediaManager.showSavedImage();
        modeController.onMedia(false);
    }
}

void loop() {
    buttonManager.loop();
    gpsManager.loop();
    connectionManager.loop();
    routeManager.loop(connectionManager.internetConnected());
#if defined(MOTOMAP_NAV_MAP)
    navigationMapManager.loop(connectionManager.internetConnected());
#endif

    const RouteState routeState = routeManager.state();
    const bool navigationActive = routeState == RouteState::WaitingGps ||
                                  routeState == RouteState::Requesting ||
                                  routeState == RouteState::Navigating ||
                                  routeState == RouteState::OffRoute ||
                                  routeState == RouteState::Error;

    if (buttonManager.wasPressed()) {
        Serial.printf("[BUTTON] Clicked! Mode: %s, hasTestImage: %s, hasSavedImage: %s\n",
                      modeName(modeController.mode()),
                      mediaManager.hasTestImage() ? "YES" : "NO",
                      mediaManager.hasSavedImage() ? "YES" : "NO");
        if (!navigationActive) {
            if (mediaManager.hasTestImage() || modeController.mode() == DeviceMode::Media) {
                mediaManager.setTestImageActive(false);
                modeController.onIdle(connectionManager.webAddress());
            } else if (mediaManager.hasSavedImage()) {
                mediaManager.showSavedImage();
                modeController.onMedia(false);
            } else {
                modeController.onMedia(true);
            }
        }
    }

    if (!navigationActive && mediaManager.hasAnimation() && !mediaManager.hasTestImage()) {
        if (modeController.mode() != DeviceMode::Media) {
            modeController.onMedia(false);
        }
    }
    mediaManager.loop(navigationActive);
    delay(2);
}
