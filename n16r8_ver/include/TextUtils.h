#pragma once

#include <Arduino.h>

// The built-in LovyanGFX bitmap font has no Vietnamese UTF-8 glyphs. Convert
// text received from geocoders/routing services to readable ASCII before it is
// sent to the TFT (for example: accented Vietnamese street names -> ASCII).
String tftSafeText(const String& input);
