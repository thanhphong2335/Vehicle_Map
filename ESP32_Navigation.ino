// ============================================================
//  ESP32_Navigation.ino
//  
//  PROJECT: ESP32 Navigation Display với Bluetooth (Chronos)
//  PHẦN CỨNG:
//    - ESP32 (CH340C USB-UART)
//    - OLED SH1106 1.3" I2C (SDA=21, SCL=22)
//  
//  THƯ VIỆN CẦN CÀI:
//    1. U8g2           by olikraus        (Arduino Library Manager)
//    2. NimBLE-Arduino  by h2zero         (Arduino Library Manager)
//  
//  BOARD: ESP32 Dev Module
//  Upload Speed: 115200
//  Flash Frequency: 80MHz
//  
//  Tác giả: ESP32 Navigation Project
//  Phiên bản: 1.0.0
// ============================================================

#include <Arduino.h>
#include "src/bluetooth_handler.h"
#include "src/display_handler.h"
#include "src/parser.h"

// ============================================================
//  OBJECTS TOÀN CỤC
// ============================================================
BluetoothHandler btHandler;     // Quản lý BLE
DisplayHandler   display;       // Quản lý màn hình OLED
NavigationParser parser;        // Parse notification

// ============================================================
//  BIẾN TRẠNG THÁI
// ============================================================
DirectionData currentDirection;          // Dữ liệu điều hướng hiện tại
unsigned long lastUpdateTime = 0;        // Thời điểm cập nhật cuối
unsigned long displayUpdateInterval = 1000; // Cập nhật màn hình mỗi 1 giây
bool hasNewData = false;                 // Có dữ liệu mới cần render
unsigned long dataExpiryMs = 30000;     // Dữ liệu hết hạn sau 30 giây

// Biến cho chức năng simulate test (nhấn nút BOOT)
#define BOOT_BUTTON_PIN 0
unsigned long lastBtnPress = 0;
int testScenario = 0;
// Hàm kiểm tra xem nội dung text có chứa từ khóa chỉ đường hay không
bool isNavigationText(String text) {
  String lower = text;
  lower.toLowerCase(); // Chuyển về chữ thường để dễ tìm kiếm

  // --- Từ khóa Tiếng Việt ---
  if (lower.indexOf("rẽ") >= 0) return true;
  // Chú ý: dùng "đi thẳng" thay vì "đi" để tránh match nhầm
  if (lower.indexOf("đi thẳng") >= 0) return true;
  if (lower.indexOf("đi theo") >= 0) return true;
  if (lower.indexOf("hướng") >= 0) return true;
  if (lower.indexOf("quay đầu") >= 0) return true;
  if (lower.indexOf("chỉ đường") >= 0) return true;
  if (lower.indexOf("vòng xuyến") >= 0) return true;
  if (lower.indexOf("đến nơi") >= 0) return true;

  // --- Từ khóa Tiếng Anh ---
  if (lower.indexOf("turn") >= 0) return true;
  if (lower.indexOf("continue") >= 0) return true;
  if (lower.indexOf("head") >= 0) return true;
  if (lower.indexOf("keep") >= 0) return true;
  if (lower.indexOf("arrived") >= 0) return true;
  if (lower.indexOf("roundabout") >= 0) return true;

  return false;
}
// ============================================================
//  CALLBACK: Nhận notification từ Bluetooth
//  Hàm này được gọi khi Chronos gửi notification
// ============================================================
void onNotificationReceived(const String& appName, const String& text) {
  Serial.println("\n=== NOTIFICATION RECEIVED ===");
  Serial.println("App:  [" + appName + "]");
  Serial.println("Text: [" + text + "]");

  // Thêm "Message" vào danh sách nhận diện vì Chronos đang đẩy appName là "Message"
  bool isFromMaps = (appName == "Google Maps") 
                 || (appName == "Maps")
                 || (appName.indexOf("maps") >= 0)      
                 || (appName.indexOf("Maps") >= 0)
                 || (appName == "Message")  // <--- CHÍNH LÀ DÒNG NÀY
                 || (appName == "test");
  
  bool hasNavKeyword = isNavigationText(text);
  
  Serial.println(">>> isFromMaps:     " + String(isFromMaps ? "YES" : "NO"));
  Serial.println(">>> hasNavKeyword:  " + String(hasNavKeyword ? "YES" : "NO"));

  if (!isFromMaps && !hasNavKeyword) {
    Serial.println(">>> SKIPPED: not Maps and no nav keywords");
    return;
  }
  
  DirectionData result = parser.parse(text);
  
  if (result.isValid) {
    currentDirection = result;
    hasNewData = true;
    lastUpdateTime = millis();
    
    Serial.println(">>> Direction: " + result.dirText);
    Serial.println(">>> Distance:  " + result.distText);
    if (result.streetName.length() > 0) {
      Serial.println(">>> Street:    " + result.streetName);
    }
    
    display.update(currentDirection);
  } else {
    Serial.println(">>> PARSE FAILED for: [" + text + "]");
  }
  Serial.println("=============================");
}

// ============================================================
//  SETUP - Chạy một lần khi khởi động
// ============================================================
void setup() {
  // Khởi động Serial để debug
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n====================================");
  Serial.println("  ESP32 Navigation Display v1.0");
  Serial.println("====================================");
  
  // Nút BOOT để test (GPIO 0 - active LOW)
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  
  // 1. Khởi động màn hình OLED
  Serial.println("[SETUP] Initializing display...");
  if (!display.begin()) {
    Serial.println("[ERROR] Display init failed! Check I2C wiring.");
  }
  display.showMessage("Starting...", "");
  delay(1000);
  
  // 2. Khởi động BLE
  Serial.println("[SETUP] Initializing Bluetooth...");
  display.showMessage("BT Starting", "Please wait...");
  
  btHandler.onNotificationReceived(onNotificationReceived);
  btHandler.begin();
  
  delay(500);
  
  // 3. Màn hình chờ kết nối
  display.showConnecting();
  
  Serial.println("[SETUP] Ready!");
  Serial.println("[SETUP] Device: ESP32-NAV");
  Serial.println("[SETUP] Open Chronos -> Settings -> Add Device -> Search for ESP32-NAV");
  Serial.println("[SETUP] Press BOOT button to test with sample data");
}

// ============================================================
//  LOOP - Chạy liên tục
// ============================================================
void loop() {
  // 1. Xử lý BLE (auto-reconnect)
  btHandler.tick();
  
  // 2. Xử lý blink khi gần điểm rẽ
  display.tick();
  
  // 3. Kiểm tra trạng thái kết nối và cập nhật màn hình
  static bool wasConnected = false;
  bool isConnected = btHandler.isConnected();
  
  // Trạng thái kết nối thay đổi
  if (isConnected != wasConnected) {
    wasConnected = isConnected;
    
    if (isConnected) {
      Serial.println("[MAIN] BT Connected - showing idle screen");
      if (!currentDirection.isValid) {
        display.showIdle(true);
      }
    } else {
      Serial.println("[MAIN] BT Disconnected - showing connecting screen");
      display.showConnecting();
    }
  }
  
  // 4. Nếu không kết nối, hiển thị animation connecting
  if (!isConnected) {
    static unsigned long lastConnAnim = 0;
    if (millis() - lastConnAnim > 500) {
      lastConnAnim = millis();
      display.showConnecting();
    }
    return; // Không làm gì thêm khi chưa kết nối
  }
  
  // 5. Kiểm tra dữ liệu hết hạn
  if (currentDirection.isValid && 
      (millis() - lastUpdateTime > dataExpiryMs)) {
    Serial.println("[MAIN] Navigation data expired");
    currentDirection.isValid = false;
    display.showIdle(true);
  }
  
  // 6. Nếu không có dữ liệu navigation -> idle screen
  if (!currentDirection.isValid && !hasNewData) {
    static unsigned long lastIdleUpdate = 0;
    if (millis() - lastIdleUpdate > 2000) {
      lastIdleUpdate = millis();
      display.showIdle(true);
    }
  }
  
  hasNewData = false;
  
  // 7. Test với nút BOOT (GPIO 0)
  handleTestButton();
  
  delay(50); // Tránh WDT reset
}

// ============================================================
//  TEST: Nhấn nút BOOT để test các scenario
// ============================================================
void handleTestButton() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    if (millis() - lastBtnPress > 800) { // Debounce 800ms
      lastBtnPress = millis();
      
      // Các chuỗi test mẫu (giống notification thật từ Google Maps)
      const char* testNotifications[] = {
        "Turn right in 200 m",
        "Turn left onto Nguyen Trai Street in 50 m",
        "Continue straight for 1.2 km",
        "Make a U-turn in 100 m",
        "Keep left at the fork in 300 m",
        "Slight right onto Le Loi in 80 m",
        "You have arrived at your destination",
        "Turn right in 15 m",   // Test blink (<20m)
        "Head north on Tran Hung Dao for 0.5 km",
      };
      
      int numTests = sizeof(testNotifications) / sizeof(testNotifications[0]);
      testScenario = (testScenario + 1) % numTests;
      
      Serial.println("\n[TEST] Simulating: " + String(testNotifications[testScenario]));
      
      // Gọi callback như thể nhận từ BLE
      onNotificationReceived("test", testNotifications[testScenario]);
    }
  }
}

// ============================================================
//  Hàm tiện ích: In thông tin debug về DirectionData
// ============================================================
void printDirectionData(const DirectionData& d) {
  Serial.println("--- Direction Data ---");
  Serial.println("Valid:    " + String(d.isValid ? "YES" : "NO"));
  Serial.println("Dir:      " + d.dirText);
  Serial.println("Dist:     " + d.distText + " (" + String(d.distance) + "m)");
  Serial.println("Street:   " + d.streetName);
  Serial.println("---------------------");
}
