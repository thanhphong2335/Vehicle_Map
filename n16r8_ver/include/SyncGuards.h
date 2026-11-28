#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstdint>

/**
 * RAII recursive mutex guarding all LittleFS file and directory operations across cores.
 */
class FsLock {
public:
    static bool init();
    explicit FsLock(TickType_t timeoutTicks = pdMS_TO_TICKS(2000));
    ~FsLock();

    FsLock(const FsLock&) = delete;
    FsLock& operator=(const FsLock&) = delete;

    bool locked() const { return _acquired; }

private:
    bool _acquired = false;
    static SemaphoreHandle_t s_mutex;
};

/**
 * RAII mutex serializing heavy outbound HTTP/HTTPS TLS requests on Core 0 to prevent internal SRAM OOM.
 */
class NetTlsGate {
public:
    static bool init();
    explicit NetTlsGate(TickType_t timeoutTicks = pdMS_TO_TICKS(15000));
    ~NetTlsGate();

    NetTlsGate(const NetTlsGate&) = delete;
    NetTlsGate& operator=(const NetTlsGate&) = delete;

    bool acquired() const { return _acquired; }

private:
    bool _acquired = false;
    static SemaphoreHandle_t s_mutex;
};

/**
 * Global atomic flag pausing background tile/route flash & network writes during OTA flashing.
 */
namespace OtaGuard {
void setActive(bool active);
bool isActive();
} // namespace OtaGuard

enum class UiActionType : uint8_t {
    ShowIdle = 0,
    EnterMediaMode,
    ShowSavedImage,
    ShowStatus,
    DrawStreamFrame,
};

struct UiAction {
    UiActionType type = UiActionType::ShowIdle;
    uint16_t color = 0xFFFF;
    bool flag = false;
    char title[32]{};
    char detail[80]{};
};

/**
 * Thread-safe POD queue allowing Core 0 tasks to request UI/SPI2 updates on Core 1 safely.
 */
namespace UiActionQueue {
bool init();
bool push(const UiAction& action, TickType_t timeoutTicks = pdMS_TO_TICKS(50));
bool pushIdle(const char* detail);
bool pushStatus(const char* title, const char* detail, uint16_t color);
bool poll(UiAction& outAction);
} // namespace UiActionQueue

namespace SyncGuards {
bool init();
} // namespace SyncGuards
