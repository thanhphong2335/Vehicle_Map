#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include "AlertManager.h"
#include "ButtonManager.h"
#include "ConnectionManager.h"
#include "DebugLog.h"
#include "DisplayManager.h"
#include "GpsManager.h"
#include "ImuManager.h"
#include "LedManager.h"
#include "MediaManager.h"
#include "ModeController.h"
#if defined(MOTOMAP_NAV_MAP)
#include "NavigationMapManager.h"
#endif
#include "PinConfig.h"
#include "RouteManager.h"
#include "SettingsStore.h"
#include "SyncGuards.h"

ButtonManager buttonManager(PinConfig::kButtonPin);
DisplayManager displayManager;
SettingsStore settingsStore;
AlertManager alertManager(&settingsStore);
GpsManager gpsManager;
ImuManager imuManager(&alertManager, &gpsManager);
MediaManager mediaManager(&displayManager);
ModeController modeController(&displayManager);
RouteManager routeManager(&gpsManager, &settingsStore);
LedManager ledManager(PinConfig::kRgbLedPin);
#if defined(MOTOMAP_NAV_MAP)
NavigationMapManager navigationMapManager(&displayManager, &routeManager);
#endif
ConnectionManager connectionManager(
    &displayManager,
    &mediaManager,
    &modeController,
    &gpsManager,
    &routeManager,
    &settingsStore
#if defined(MOTOMAP_NAV_MAP)
    , &navigationMapManager
#else
    , nullptr
#endif
    , &alertManager
    , &imuManager
);

namespace {
bool s_mediaDismissedByUser = false;

bool isNavigationActiveState(RouteState routeState) {
    return routeState == RouteState::WaitingGps ||
           routeState == RouteState::Requesting ||
           routeState == RouteState::Navigating ||
           routeState == RouteState::OffRoute ||
           routeState == RouteState::Error;
}

void confirmOtaBootValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) return;

    esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &otaState) == ESP_OK &&
        otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            Serial.printf("[OTA] Partition '%s' verified and marked valid.\n", running->label);
        } else {
            Serial.printf("[OTA] WARNING: Failed to mark partition '%s' valid!\n", running->label);
        }
    }
}

void processPendingUiActions() {
    const RouteState routeState = routeManager.state();
    const bool navActive = isNavigationActiveState(routeState);
    const bool activelyGuiding = (routeState == RouteState::Navigating ||
                                  routeState == RouteState::OffRoute);
    UiAction action{};
    while (UiActionQueue::poll(action)) {
        if (action.type == UiActionType::ShowIdle) {
            if (activelyGuiding) {
                continue;
            }
        } else if (navActive && action.type != UiActionType::ShowStatus) {
            continue;
        }
        switch (action.type) {
            case UiActionType::ShowIdle:
            case UiActionType::ClearTestImageAndIdle:
            case UiActionType::DeleteAnimationAndIdle:
                s_mediaDismissedByUser = true;
                modeController.onIdle(action.detail[0] ? String(action.detail) : String("Waiting for iPhone"));
                break;

            case UiActionType::ShowMediaPlaceholder:
                s_mediaDismissedByUser = false;
                modeController.onMedia(true);
                break;

            case UiActionType::EnterMediaMode:
                s_mediaDismissedByUser = false;
                modeController.onMedia(action.flag);
                break;

            case UiActionType::ShowSavedImage:
                if (mediaManager.showSavedImage()) {
                    s_mediaDismissedByUser = false;
                    modeController.onMedia(false);
                }
                break;

            case UiActionType::ReloadAndCacheAnimation:
                break;

            case UiActionType::ShowStatus:
                displayManager.showStatus(action.title, action.detail, action.color);
                break;

            case UiActionType::DrawStreamFrame:
                if (mediaManager.drawPendingStreamFrame()) {
                    s_mediaDismissedByUser = false;
                    modeController.onMedia(false);
                }
                break;
        }
    }
}
} // namespace

void onNavigationUpdate(const NavigationData& data) {
#if defined(MOTOMAP_NAV_MAP)
    if (data.state == NavigationState::Stop || data.state == NavigationState::Arrived) {
        navigationMapManager.onNavigation(data);
        const String detail = (data.state == NavigationState::Arrived)
            ? String("Arrived")
            : (connectionManager.internetConnected()
                ? connectionManager.webAddress()
                : String("Waiting for iPhone"));
        modeController.onIdle(detail);
        return;
    }
    const RouteState rs = routeManager.state();
    const bool shouldFocusDisplay = (data.state == NavigationState::Start) ||
                                    data.gpsValid ||
                                    (rs == RouteState::Navigating) ||
                                    (rs == RouteState::OffRoute);
    modeController.onNavigation(data, false, shouldFocusDisplay);
    navigationMapManager.onNavigation(data);
#else
    modeController.onNavigation(data);
#endif
}

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!SyncGuards::init()) {
        Serial.println("[SYS] ERROR: Failed to initialize SyncGuards!");
    }

    DEBUG_LOG_INIT();
    setCpuFrequencyMhz(240); // Set ESP32-S3 maximum CPU frequency to 240MHz
    Serial.println("\n==============================================");
    Serial.println("  MotoMap ESP32-S3 N16R8 (Active Object Dual-Core)");
    Serial.println("==============================================");

    if (psramInit()) {
        Serial.printf("[PSRAM] Initialized successfully! Size: %u MB, Free: %u KB\n",
                      ESP.getPsramSize() / (1024 * 1024),
                      ESP.getFreePsram() / 1024);
    } else {
        Serial.println("[PSRAM] WARNING: PSRAM not initialized or not detected!");
    }

    // Initialize Task Watchdog Timer (10s timeout, panic on hang) before spawning Core 0 tasks
    esp_task_wdt_init(10, true);

    buttonManager.begin();
    ledManager.begin();
    displayManager.begin();
    settingsStore.begin();
    alertManager.begin();
    imuManager.begin();
    gpsManager.begin();
    mediaManager.begin();
#if defined(MOTOMAP_NAV_MAP)
    navigationMapManager.begin();
#endif
    modeController.begin();
    routeManager.begin(onNavigationUpdate);

    connectionManager.setLedManager(&ledManager);
    connectionManager.begin();

    if (!isNavigationActiveState(routeManager.state()) && mediaManager.hasSavedImage()) {
        mediaManager.showSavedImage();
        modeController.onMedia(false);
    }

    confirmOtaBootValid();
    esp_task_wdt_add(nullptr); // Subscribe Core 1 loopTask after initial asset load
}

void loop() {
    esp_task_wdt_reset();
    processPendingUiActions();

    buttonManager.loop();
    ledManager.loop();
    gpsManager.loop();

    if (OtaGuard::isActive()) {
        delay(10);
        return;
    }

    const bool internetOk = connectionManager.internetConnected();
    routeManager.loop(internetOk);
#if defined(MOTOMAP_NAV_MAP)
    navigationMapManager.loop(internetOk);
#endif

    const RouteState routeState = routeManager.state();
    const bool navigationActive = isNavigationActiveState(routeState);

    static uint32_t s_lastLedUpdate = 0;
    if (millis() - s_lastLedUpdate >= 100) {
        s_lastLedUpdate = millis();
        const bool fall = imuManager.isFallDetected();
        const bool nav = (routeState == RouteState::Navigating);
        const bool turn = nav && (routeManager.currentNavigation().distanceToTurnM > 0 &&
                                  routeManager.currentNavigation().distanceToTurnM <= 80);
        const bool gpsOk = gpsManager.snapshot().valid;
        ledManager.updateFromSystem(fall, nav, turn, gpsOk, internetOk);
    }

    if (buttonManager.wasPressed()) {
        Serial.printf("[BUTTON] Clicked! Mode: %s, hasTestImage: %s, hasSavedImage: %s\n",
                      modeName(modeController.mode()),
                      mediaManager.hasTestImage() ? "YES" : "NO",
                      mediaManager.hasSavedImage() ? "YES" : "NO");
        if (!navigationActive) {
            if (mediaManager.hasTestImage() || modeController.mode() == DeviceMode::Media) {
                mediaManager.setTestImageActive(false);
                s_mediaDismissedByUser = true;
                if (internetOk) {
                    modeController.onIdle(connectionManager.webAddress());
                } else {
                    modeController.onIdle("Waiting for iPhone");
                }
            } else if (mediaManager.hasSavedImage()) {
                s_mediaDismissedByUser = false;
                mediaManager.showSavedImage();
                modeController.onMedia(false);
            } else if (mediaManager.hasAnimation()) {
                s_mediaDismissedByUser = false;
                modeController.onMedia(false);
            } else {
                modeController.onMedia(true);
            }
        }
    }

    if (!navigationActive && !s_mediaDismissedByUser && mediaManager.hasAnimation() && !mediaManager.hasTestImage()) {
        if (modeController.mode() != DeviceMode::Media) {
            modeController.onMedia(false);
        }
    }
    mediaManager.loop(navigationActive);
    delay(2);
}
