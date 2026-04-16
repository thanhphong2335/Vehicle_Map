#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "parser.h"


#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define I2C_SDA 21
#define I2C_SCL 22
#define OLED_I2C_ADDR 0x3C
#define BLINK_INTERVAL_MS 400
#define BLINK_DISTANCE_THRESHOLD 20

class DisplayHandler {
public:
  DisplayHandler();
  bool begin();
  void update(const DirectionData& data);
  void showIdle(bool btConnected = false);
  void showConnecting();
  void showMessage(const String& line1, const String& line2 = "");
  void tick();
  void off();
  void on();
  void renderNavigation(); // public để gọi được từ tick()

private:
  U8G2_SH1106_128X64_NONAME_F_HW_I2C _display;
  DirectionData _currentData;
  bool _blinking;
  bool _blinkState;
  unsigned long _lastBlink;
  bool _isOn;
  
  // Các hàm vẽ đã được cập nhật
  void drawArrow(DirectionType dir, int x, int y, bool filled, bool visible);
  void drawDistance(const String& distText);
  void drawStreetName(const String& street);
  void drawBTIcon(bool connected);
};

#endif
