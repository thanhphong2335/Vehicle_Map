#include "AlertManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctime>
#include <cstring>

#include "SyncGuards.h"

namespace {
constexpr size_t kQueueLength = 4;
constexpr uint32_t kWorkerStackSize = 8192;
constexpr UBaseType_t kWorkerPriority = 3;
constexpr BaseType_t kWorkerCore = 0; // Run on Core 0 to prevent UI and GPS contention on Core 1
constexpr uint32_t kHttpTimeoutMs = 7000;
constexpr char kGatewayCaPath[] = "/certs/gateway_ca.pem";
constexpr time_t kMinValidEpoch = 1700000000; // Nov 2023+ confirms SNTP clock sync
} // namespace

AlertManager::AlertManager(SettingsStore* settings)
    : _settings(settings) {}

AlertManager::~AlertManager() {
    if (_workerTaskHandle != nullptr) {
        vTaskDelete(_workerTaskHandle);
        _workerTaskHandle = nullptr;
    }
    if (_alertQueue != nullptr) {
        vQueueDelete(_alertQueue);
        _alertQueue = nullptr;
    }
    if (_statusMutex != nullptr) {
        vSemaphoreDelete(_statusMutex);
        _statusMutex = nullptr;
    }
}

void AlertManager::setLastStatus(const char* status) {
    if (_statusMutex == nullptr || xSemaphoreTake(_statusMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (status == nullptr) {
            _lastStatus[0] = '\0';
        } else {
            strncpy(_lastStatus, status, sizeof(_lastStatus) - 1);
            _lastStatus[sizeof(_lastStatus) - 1] = '\0';
        }
        if (_statusMutex != nullptr) {
            xSemaphoreGive(_statusMutex);
        }
    }
}

bool AlertManager::begin() {
    if (_statusMutex == nullptr) {
        _statusMutex = xSemaphoreCreateMutex();
        if (_statusMutex == nullptr) {
            Serial.println("[ALERT] ERROR: Failed to create status mutex!");
            return false;
        }
    }

    if (_alertQueue == nullptr) {
        _alertQueue = xQueueCreate(kQueueLength, sizeof(AlertEvent));
        if (_alertQueue == nullptr) {
            Serial.println("[ALERT] ERROR: Failed to create alert queue!");
            return false;
        }
    }

    if (_workerTaskHandle == nullptr) {
        const BaseType_t result = xTaskCreatePinnedToCore(
            workerTask,
            "AlertWorker",
            kWorkerStackSize,
            this,
            kWorkerPriority,
            &_workerTaskHandle,
            kWorkerCore
        );

        if (result != pdPASS) {
            Serial.println("[ALERT] ERROR: Failed to spawn AlertWorker task!");
            return false;
        }
    }

    Serial.println("[ALERT] Module initialized successfully.");
    return true;
}

bool AlertManager::triggerAlert(const AlertEvent& event) {
    if (_alertQueue == nullptr) {
        return false;
    }

    const BaseType_t sent = xQueueSend(_alertQueue, &event, 0);
    if (sent != pdPASS) {
        Serial.println("[ALERT] WARNING: Alert queue is full, dropping event!");
        return false;
    }

    Serial.println("[ALERT] Event queued successfully.");
    return true;
}

bool AlertManager::triggerTestAlert(const GpsSnapshot& gps) {
    AlertEvent event{};
    strncpy(event.event, "System Alert Test (Manual Trigger)", sizeof(event.event) - 1);
    event.latitude = gps.valid ? gps.latitude : 10.776889;
    event.longitude = gps.valid ? gps.longitude : 106.656000;
    event.speedKmh = gps.valid ? gps.speedKmh : 0.0F;
    event.courseDeg = gps.valid ? gps.courseDeg : 0.0F;
    strncpy(event.status, "Manually triggered via Web Portal", sizeof(event.status) - 1);
    return triggerAlert(event);
}

bool AlertManager::isBusy() const {
    return _isBusy.load(std::memory_order_acquire);
}

String AlertManager::lastStatus() const {
    String copy;
    if (_statusMutex == nullptr || xSemaphoreTake(_statusMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = _lastStatus;
        if (_statusMutex != nullptr) {
            xSemaphoreGive(_statusMutex);
        }
    } else {
        copy = "Busy";
    }
    return copy;
}

void AlertManager::workerTask(void* param) {
    auto* manager = static_cast<AlertManager*>(param);
    manager->runWorker();
}

void AlertManager::runWorker() {
    AlertEvent event;
    while (true) {
        if (xQueueReceive(_alertQueue, &event, portMAX_DELAY) == pdPASS) {
            _isBusy.store(true, std::memory_order_release);
            Serial.printf("[ALERT] Worker received event: %s\n", event.event);

            if (_settings == nullptr) {
                setLastStatus("Error: SettingsStore not initialized");
                _isBusy.store(false, std::memory_order_release);
                continue;
            }

            AlertConfig config;
            if (!_settings->loadAlertConfig(config) || !config.enabled) {
                setLastStatus("Alerts are currently disabled");
                Serial.println("[ALERT] Alert feature is disabled in settings.");
                _isBusy.store(false, std::memory_order_release);
                continue;
            }

            if (config.gatewayUrl.isEmpty()) {
                setLastStatus("Cloudflare Gateway URL not configured");
                Serial.println("[ALERT] Gateway URL is empty. Aborting.");
                _isBusy.store(false, std::memory_order_release);
                continue;
            }

            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[ALERT] Wi-Fi not connected! Waiting up to 3s...");
                const uint32_t waitStart = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - waitStart < 3000) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }

            if (WiFi.status() != WL_CONNECTED) {
                setLastStatus("Error: No Wi-Fi or Hotspot connection");
                Serial.println("[ALERT] Cannot send alert: No Internet connection.");
                _isBusy.store(false, std::memory_order_release);
                continue;
            }

            sendAlertPayload(event, config);
            _isBusy.store(false, std::memory_order_release);
        }
    }
}

bool AlertManager::sendAlertPayload(const AlertEvent& event, const AlertConfig& config) {
    JsonDocument doc;
    doc["event"] = event.event;
    doc["latitude"] = event.latitude;
    doc["longitude"] = event.longitude;
    doc["speedKmh"] = event.speedKmh;
    doc["courseDeg"] = event.courseDeg;
    doc["status"] = event.status;
    doc["deviceId"] = "MotoMap-S3";

    // Parse comma-separated Chat IDs
    JsonArray chatArray = doc["chatIds"].to<JsonArray>();
    String rawChats = config.chatIds;
    rawChats.trim();
    int startIdx = 0;
    while (startIdx < rawChats.length()) {
        int commaIdx = rawChats.indexOf(',', startIdx);
        if (commaIdx == -1) {
            commaIdx = rawChats.length();
        }
        String item = rawChats.substring(startIdx, commaIdx);
        item.trim();
        if (item.length() > 0) {
            chatArray.add(item);
        }
        startIdx = commaIdx + 1;
    }

    String jsonBody;
    serializeJson(doc, jsonBody);

    // Load optional Root CA from LittleFS when system clock is SNTP-synced
    String caPem;
    const time_t nowEpoch = std::time(nullptr);
    if (nowEpoch >= kMinValidEpoch) {
        FsLock fsLock(pdMS_TO_TICKS(500));
        if (fsLock.locked() && LittleFS.exists(kGatewayCaPath)) {
            File caFile = LittleFS.open(kGatewayCaPath, "r");
            if (caFile && caFile.size() > 0 && caFile.size() < 8192) {
                caPem = caFile.readString();
            }
            if (caFile) caFile.close();
        }
    }

    // Serialize outbound TLS allocation across Core 0 tasks
    NetTlsGate tlsGate(pdMS_TO_TICKS(15000));
    if (!tlsGate.acquired()) {
        setLastStatus("Error: Network busy (TLS gate timeout)");
        Serial.println("[ALERT] NetTlsGate timeout!");
        return false;
    }

    Serial.printf("[ALERT] Dispatching to Gateway: %s\n", config.gatewayUrl.c_str());

    WiFiClientSecure client;
    if (!caPem.isEmpty()) {
        client.setCACert(caPem.c_str());
    } else {
        client.setInsecure();
    }
    client.setTimeout(kHttpTimeoutMs / 1000U);

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);

    if (!http.begin(client, config.gatewayUrl)) {
        setLastStatus("Error: Cannot connect to Gateway URL");
        Serial.println("[ALERT] http.begin() failed!");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    if (!config.authKey.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + config.authKey);
    }

    const int httpCode = http.POST(jsonBody);
    bool ok = false;
    char statusBuf[96];
    if (httpCode >= 200 && httpCode < 300) {
        snprintf(statusBuf, sizeof(statusBuf), "Sent successfully (HTTP %d)", httpCode);
        setLastStatus(statusBuf);
        Serial.printf("[ALERT] Sent successfully! Response: %s\n", http.getString().c_str());
        ok = true;
    } else {
        snprintf(statusBuf, sizeof(statusBuf), "Gateway error (HTTP %d)", httpCode);
        setLastStatus(statusBuf);
        Serial.printf("[ALERT] Gateway responded with error code %d: %s\n", httpCode, http.getString().c_str());
    }

    http.end();
    return ok;
}
