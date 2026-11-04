#include "DebugLog.h"

#if MOTOMAP_DEBUG

#include <LittleFS.h>
#include <WiFi.h>
#include <stdarg.h>

namespace DebugLog {

static constexpr size_t kBufferSize = 8192;
static char sLogBuffer[kBufferSize];
static size_t sBufferHead = 0;
static bool sBufferWrapped = false;
static portMUX_TYPE sLogMux = portMUX_INITIALIZER_UNLOCKED;
static bool sInitialized = false;

void init() {
    if (sInitialized) return;
    portENTER_CRITICAL(&sLogMux);
    sBufferHead = 0;
    sBufferWrapped = false;
    sLogBuffer[0] = '\0';
    sInitialized = true;
    portEXIT_CRITICAL(&sLogMux);

    log("SYS", "MotoMap DebugLog system initialized.");
}

void log(const char* tag, const char* format, ...) {
    char msg[384];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    const uint32_t ms = millis();
    const uint32_t secs = ms / 1000;
    const uint32_t mins = secs / 60;
    const uint32_t hours = mins / 60;

    char line[448];
    snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u][%s] %s\n",
             hours % 24, mins % 60, secs % 60, ms % 1000,
             tag ? tag : "APP", msg);

    // 1. Output directly to Serial Monitor
    Serial.print(line);

    // 2. Append to circular in-memory buffer
    portENTER_CRITICAL(&sLogMux);
    const size_t len = strlen(line);
    for (size_t i = 0; i < len; ++i) {
        sLogBuffer[sBufferHead++] = line[i];
        if (sBufferHead >= kBufferSize) {
            sBufferHead = 0;
            sBufferWrapped = true;
        }
    }
    portEXIT_CRITICAL(&sLogMux);

    // 3. Append to LittleFS /debug.log if filesystem is available
    File f = LittleFS.open("/debug.log", "a");
    if (f) {
        if (f.size() > 32768) {
            f.close();
            LittleFS.remove("/debug.log");
            f = LittleFS.open("/debug.log", "w");
        }
        if (f) {
            f.print(line);
            f.close();
        }
    }
}

String getLogs() {
    String result;
    portENTER_CRITICAL(&sLogMux);
    if (sBufferWrapped) {
        result.reserve(kBufferSize + 128);
        result.concat(sLogBuffer + sBufferHead, kBufferSize - sBufferHead);
        result.concat(sLogBuffer, sBufferHead);
    } else {
        result.reserve(sBufferHead + 64);
        result.concat(sLogBuffer, sBufferHead);
    }
    portEXIT_CRITICAL(&sLogMux);
    return result;
}

void clearLogs() {
    portENTER_CRITICAL(&sLogMux);
    sBufferHead = 0;
    sBufferWrapped = false;
    sLogBuffer[0] = '\0';
    portEXIT_CRITICAL(&sLogMux);

    if (LittleFS.exists("/debug.log")) {
        LittleFS.remove("/debug.log");
    }
}

const char* wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD: return "WL_NO_SHIELD";
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
        default: return "WL_UNKNOWN";
    }
}

} // namespace DebugLog

#endif
