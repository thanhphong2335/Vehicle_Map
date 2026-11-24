#include "SyncGuards.h"

#include <cstring>

SemaphoreHandle_t FsLock::s_mutex = nullptr;
SemaphoreHandle_t NetTlsGate::s_mutex = nullptr;

namespace {
std::atomic<bool> s_otaActive{false};
QueueHandle_t s_uiQueue = nullptr;
constexpr size_t kUiQueueDepth = 10;

void copySafe(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}
} // namespace

bool FsLock::init() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateRecursiveMutex();
    }
    return s_mutex != nullptr;
}

FsLock::FsLock(TickType_t timeoutTicks) {
    if (s_mutex != nullptr) {
        _acquired = (xSemaphoreTakeRecursive(s_mutex, timeoutTicks) == pdTRUE);
    }
}

FsLock::~FsLock() {
    if (_acquired && s_mutex != nullptr) {
        xSemaphoreGiveRecursive(s_mutex);
    }
}

bool NetTlsGate::init() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    return s_mutex != nullptr;
}

NetTlsGate::NetTlsGate(TickType_t timeoutTicks) {
    if (s_mutex != nullptr) {
        _acquired = (xSemaphoreTake(s_mutex, timeoutTicks) == pdTRUE);
    }
}

NetTlsGate::~NetTlsGate() {
    if (_acquired && s_mutex != nullptr) {
        xSemaphoreGive(s_mutex);
    }
}

namespace OtaGuard {
void setActive(bool active) {
    s_otaActive.store(active, std::memory_order_release);
}

bool isActive() {
    return s_otaActive.load(std::memory_order_acquire);
}
} // namespace OtaGuard

namespace UiActionQueue {
bool init() {
    if (s_uiQueue == nullptr) {
        s_uiQueue = xQueueCreate(kUiQueueDepth, sizeof(UiAction));
    }
    return s_uiQueue != nullptr;
}

bool push(const UiAction& action, TickType_t timeoutTicks) {
    if (s_uiQueue == nullptr) return false;
    return xQueueSend(s_uiQueue, &action, timeoutTicks) == pdTRUE;
}

bool pushIdle(const char* detail) {
    UiAction action{};
    action.type = UiActionType::ShowIdle;
    copySafe(action.detail, sizeof(action.detail), detail);
    return push(action);
}

bool pushStatus(const char* title, const char* detail, uint16_t color) {
    UiAction action{};
    action.type = UiActionType::ShowStatus;
    action.color = color;
    copySafe(action.title, sizeof(action.title), title);
    copySafe(action.detail, sizeof(action.detail), detail);
    return push(action);
}

bool poll(UiAction& outAction) {
    if (s_uiQueue == nullptr) return false;
    return xQueueReceive(s_uiQueue, &outAction, 0) == pdTRUE;
}
} // namespace UiActionQueue

namespace SyncGuards {
bool init() {
    const bool fsOk = FsLock::init();
    const bool netOk = NetTlsGate::init();
    const bool uiOk = UiActionQueue::init();
    return fsOk && netOk && uiOk;
}
} // namespace SyncGuards
