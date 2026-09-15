#pragma once

#include <stdint.h>

// Select the display through PlatformIO. The GMT020-02-7P uses the same
// ST7789 family as the original panel but exposes the full 240 x 320 area.
namespace DisplayConfig {
#if defined(MOTOMAP_DISPLAY_GMT020)
constexpr int32_t kWidth = 320;
constexpr int32_t kHeight = 240;
constexpr int32_t kMemoryWidth = 240;
constexpr int32_t kMemoryHeight = 320;
constexpr int32_t kPanelWidth = 240;
constexpr int32_t kPanelHeight = 320;
constexpr int32_t kOffsetX = 0;
constexpr int32_t kOffsetY = 0;
constexpr bool kHasBacklightPin = false; // GMT020-02-7P has no BL header pin.
#else
constexpr int32_t kWidth = 320;
constexpr int32_t kHeight = 172;
constexpr int32_t kMemoryWidth = 240;
constexpr int32_t kMemoryHeight = 320;
constexpr int32_t kPanelWidth = 172;
constexpr int32_t kPanelHeight = 320;
constexpr int32_t kOffsetX = 34;
constexpr int32_t kOffsetY = 0;
constexpr bool kHasBacklightPin = true;
#endif

constexpr int kBacklightPin = 32;
} // namespace DisplayConfig
