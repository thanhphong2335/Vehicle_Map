#pragma once

#include <Arduino.h>
#include <WiFiType.h>

// Set MOTOMAP_DEBUG to 1 in platformio.ini build_flags (-DMOTOMAP_DEBUG=1) to enable verbose diagnostic logging.
// By default in release mode, it is set to 0 and all logging macros compile away to zero code and memory.
#ifndef MOTOMAP_DEBUG
#define MOTOMAP_DEBUG 0
#endif

#if MOTOMAP_DEBUG

namespace DebugLog {
    void init();
    void log(const char* tag, const char* format, ...) __attribute__((format(printf, 2, 3)));
    String getLogs();
    void clearLogs();
    const char* wifiStatusName(wl_status_t status);
}

#define DEBUG_LOG(tag, fmt, ...) DebugLog::log(tag, fmt, ##__VA_ARGS__)
#define DEBUG_LOG_INIT()         DebugLog::init()

#else

#define DEBUG_LOG(tag, fmt, ...) ((void)0)
#define DEBUG_LOG_INIT()         ((void)0)

namespace DebugLog {
    inline void init() {}
    inline String getLogs() { return ""; }
    inline void clearLogs() {}
    inline const char* wifiStatusName(wl_status_t) { return ""; }
}

#endif
