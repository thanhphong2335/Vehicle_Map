#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>

#include "MotoMapTypes.h"
#include "SettingsStore.h"

class AlertManager {
public:
    explicit AlertManager(SettingsStore* settings);
    ~AlertManager();

    bool begin();
    bool triggerAlert(const AlertEvent& event);
    bool triggerTestAlert(const GpsSnapshot& gps);
    bool isBusy() const;
    String lastStatus() const;

private:
    SettingsStore* _settings = nullptr;
    QueueHandle_t _alertQueue = nullptr;
    TaskHandle_t _workerTaskHandle = nullptr;
    mutable SemaphoreHandle_t _statusMutex = nullptr;
    std::atomic<bool> _isBusy{false};
    char _lastStatus[96] = "Idle";

    void setLastStatus(const char* status);
    static void workerTask(void* param);
    void runWorker();
    bool sendAlertPayload(const AlertEvent& event, const AlertConfig& config);
};
