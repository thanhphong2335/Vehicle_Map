#include "display_handler.h"
#include "icons.h"
// ============================================================
//  DISPLAY_HANDLER.CPP - Triển khai hiển thị OLED SH1106
//  Layout màn hình (128x64):
//  +------------------+------------------+
//  |                  |       "in"       |
//  |   [ARROW ICON]   |   "200 m"       |
//  |                  | [====----] bar   |
//  +------------------+------------------+
//  |         Nguyen Trai Street          |
//  +-------------------------------------+
// ============================================================

DisplayHandler::DisplayHandler()
  : _display(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA),
    _blinking(false),
    _blinkState(true),
    _lastBlink(0),
    _isOn(true)
{
  _currentData.isValid = false;
}

bool DisplayHandler::begin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  _display.begin();
  _display.setContrast(200);
  
  _display.clearBuffer();
  _display.setFont(u8g2_font_6x10_tf);
  _display.drawStr(15, 25, "ESP32 Navigation");
  _display.drawStr(25, 42, "Starting...");
  _display.sendBuffer();
  
  Serial.println("[DISPLAY] SH1106 OLED initialized OK");
  return true;
}

void DisplayHandler::update(const DirectionData& data) {
  _currentData = data;
  
  if (data.isValid && data.distance > 0 && data.distance <= BLINK_DISTANCE_THRESHOLD) {
    _blinking = true;
  } else {
    _blinking = false;
    _blinkState = true;
  }
  
  renderNavigation();
}

void DisplayHandler::tick() {
  if (!_blinking) return;
  
  unsigned long now = millis();
  if (now - _lastBlink >= BLINK_INTERVAL_MS) {
    _lastBlink = now;
    _blinkState = !_blinkState;
    renderNavigation();
  }
}

void DisplayHandler::showIdle(bool btConnected) {
  _blinking = false;
  
  _display.clearBuffer();
  drawBTIcon(btConnected);
  
  _display.setFont(u8g2_font_7x13B_tf);
  _display.drawStr(5, 28, "No Navigation");
  
  _display.setFont(u8g2_font_5x7_tf);
  if (btConnected) {
    _display.drawStr(15, 44, "BT Connected");
    _display.drawStr(5, 55, "Open Google Maps...");
  } else {
    _display.drawStr(5, 44, "Open Chronos App");
    _display.drawStr(8, 55, "to connect BT");
  }
  
  _display.sendBuffer();
}

void DisplayHandler::showConnecting() {
  static int dots = 0;
  static unsigned long lastDot = 0;
  
  if (millis() - lastDot > 500) {
    dots = (dots + 1) % 4;
    lastDot = millis();
  }
  
  _display.clearBuffer();
  _display.setFont(u8g2_font_7x13B_tf);
  _display.drawStr(10, 22, "Connecting BT");
  
  _display.setFont(u8g2_font_5x7_tf);
  _display.drawStr(8, 38, "Device: ESP32-NAV");
  
  String dotStr = "Waiting";
  for (int i = 0; i < dots; i++) dotStr += ".";
  _display.drawStr(10, 54, dotStr.c_str());
  
  _display.sendBuffer();
}

void DisplayHandler::showMessage(const String& line1, const String& line2) {
  _display.clearBuffer();
  _display.setFont(u8g2_font_7x13B_tf);
  
  int w1 = _display.getStrWidth(line1.c_str());
  _display.drawStr((128 - w1) / 2, 28, line1.c_str());
  
  if (line2.length() > 0) {
    _display.setFont(u8g2_font_6x10_tf);
    int w2 = _display.getStrWidth(line2.c_str());
    _display.drawStr((128 - w2) / 2, 46, line2.c_str());
  }
  
  _display.sendBuffer();
}

// ============================================================
//  RENDER CHÍNH - Vẽ toàn bộ màn hình navigation
// ============================================================
void DisplayHandler::renderNavigation() {
  if (!_currentData.isValid) {
    showIdle(true);
    return;
  }
  
  _display.clearBuffer(); // 1. Xóa buffer (Tránh Flicker)
  
  // 2. Icon Bluetooth góc phải
  drawBTIcon(true);
  
  // 3. Tính toán Animation (Trượt lên xuống nhẹ 2 pixel để báo hiệu xe đang chạy)
  int yOffset = (millis() / 300) % 3; 
  if (_currentData.direction == DIR_ARRIVE) yOffset = 0; // Đã đến nơi thì đứng im
  
  // 4. Vẽ mũi tên căn giữa phía trên
  int arrowX = (OLED_WIDTH - ICON_SIZE) / 2; // Căn giữa màn hình (128 - 24) / 2
  int arrowY = 4 - yOffset; // Nằm sát mép trên, có hiệu ứng trượt
  
  drawArrow(_currentData.direction, arrowX, arrowY, true, _blinkState);

  // 5. Vẽ khoảng cách lớn, căn giữa
  if (_currentData.direction == DIR_ARRIVE) {
    drawDistance("Arrived!");
  } else if (_currentData.distance >= 0) {
    drawDistance(_currentData.distText);
  }
  
  // 6. Tên đường ở dưới cùng
  if (_currentData.streetName.length() > 0) {
    drawStreetName(_currentData.streetName);
  }
  
  _display.sendBuffer(); // 7. Đẩy ra màn hình (Chỉ gọi 1 lần duy nhất)
}

// ============================================================
//  HÀM DRAW ARROW CHUYÊN NGHIỆP - SỬ DỤNG BITMAP (XBM)
// ============================================================
void DisplayHandler::drawArrow(DirectionType dir, int x, int y, bool filled, bool visible) {
  if (!visible) return; // Dùng cho hiệu ứng nhấp nháy

  const unsigned char* bitmap = nullptr;

  switch (dir) {
    case DIR_LEFT:
    case DIR_SHARP_L:
      bitmap = icon_left_filled;
      break;
    case DIR_RIGHT:
    case DIR_SHARP_R:
      bitmap = filled ? icon_right_filled : icon_right_outline;
      break;
    case DIR_UTURN:
    case DIR_ROUNDABOUT:
      bitmap = icon_uturn_filled;
      break;
    case DIR_STRAIGHT:
    case DIR_SLIGHT_L: // Dùng tạm straight cho rẽ nhẹ
    case DIR_SLIGHT_R:
    case DIR_ARRIVE:
    default:
      bitmap = filled ? icon_straight_filled : icon_straight_outline;
      break;
  }

  if (bitmap != nullptr) {
    _display.drawXBMP(x, y, ICON_SIZE, ICON_SIZE, bitmap);
  }
}

// ============================================================
//  HIỂN THỊ KHOẢNG CÁCH (Font To, Rõ ràng)
// ============================================================
void DisplayHandler::drawDistance(const String& distText) {
  if (distText.length() == 0) return;

  // Sử dụng font đậm (Bold) cho số khoảng cách
  if (distText.length() <= 5) {
    _display.setFont(u8g2_font_9x15B_tf); // Font to cho số ngắn (VD: "200 m")
  } else {
    _display.setFont(u8g2_font_7x13B_tf); // Font vừa cho số dài (VD: "12.5 km")
  }
  
  int textWidth = _display.getStrWidth(distText.c_str());
  int x = (OLED_WIDTH - textWidth) / 2; // Căn giữa
  int y = 44; // Nằm dưới icon mũi tên
  
  _display.drawStr(x, y, distText.c_str());
}

// ============================================================
//  HIỂN THỊ TÊN ĐƯỜNG
// ============================================================
void DisplayHandler::drawStreetName(const String& street) {
  _display.setFont(u8g2_font_6x10_tf); // Font gọn, nét đều
  
  String disp = street;
  // Cắt bớt nếu tên đường quá dài
  if (disp.length() > 20) {
    disp = disp.substring(0, 18) + "..";
  }
  
  int w = _display.getStrWidth(disp.c_str());
  int x = (OLED_WIDTH - w) / 2;
  
  // Kẻ một đường viền ngang mỏng cách điệu
  _display.drawHLine(10, 52, OLED_WIDTH - 20);
  _display.drawStr(x, 63, disp.c_str());
}

// ============================================================
//  ICON BLUETOOTH (góc trên phải)
// ============================================================
void DisplayHandler::drawBTIcon(bool connected) {
  _display.setFont(u8g2_font_4x6_tf);
  _display.drawStr(112, 7, "BT");
  
  if (!connected) {
    // Gạch chéo qua chữ BT
    _display.drawLine(111, 1, 122, 8);
  }
}

void DisplayHandler::off() {
  _display.setPowerSave(1);
  _isOn = false;
}

void DisplayHandler::on() {
  _display.setPowerSave(0);
  _isOn = true;
}
