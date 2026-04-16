#include "bluetooth_handler.h"

// Khởi tạo con trỏ instance để dùng trong static callback
BluetoothHandler* BluetoothHandler::_instance = nullptr;

BluetoothHandler::BluetoothHandler() 
  : _chronos("ESP32-NAV"), // Khởi tạo Chronos với tên thiết bị
    _connected(false), 
    _callback(nullptr) 
{
  _instance = this;
}

void BluetoothHandler::begin() {
  Serial.println("[BLE] Initializing ChronosESP32...");
  
  // Đăng ký các sự kiện với ChronosESP32
  _chronos.setConnectionCallback(onConnectionChange);
  _chronos.setNotificationCallback(onNotify);
  
  // Khởi động BLE. Thư viện sẽ tự cấu hình UUID và Advertising
  _chronos.begin(); 
  
  Serial.println("[BLE] Server started, waiting for Chronos app...");
}

void BluetoothHandler::tick() {
  // Bắt buộc gọi liên tục trong loop() để thư viện xử lý gói tin
  _chronos.loop(); 
}

bool BluetoothHandler::isConnected() const {
  return _connected;
}

void BluetoothHandler::onNotificationReceived(NotificationCallback cb) {
  _callback = cb;
}

// --- STATIC CALLBACKS ---

void BluetoothHandler::onConnectionChange(bool connected) {
  if (_instance) {
    _instance->_connected = connected;
    if (connected) {
      Serial.println("[BLE] ✓ Device connected via Chronos!");
    } else {
      Serial.println("[BLE] ✗ Device disconnected.");
    }
  }
}

void BluetoothHandler::onNotify(Notification notification) {
  if (_instance) {
    _instance->_lastAppName = notification.app;
    
    // DEBUG: In toàn bộ raw data trước khi xử lý
    Serial.println("\n=== RAW BLE NOTIFICATION ===");
    Serial.println("RAW app:     [" + notification.app + "]");
    Serial.println("RAW title:   [" + notification.title + "]");
    Serial.println("RAW message: [" + notification.message + "]");
    Serial.println("============================");
    
    if (_instance->_callback) {
      // Thử title trước (Maps thường để chỉ dẫn ở title)
      // Nếu title rỗng thì dùng message, nếu message rỗng thì dùng title
      String fullText;
      if (notification.title.length() > 0 && notification.message.length() > 0) {
        fullText = notification.title + " " + notification.message;
      } else if (notification.title.length() > 0) {
        fullText = notification.title;
      } else {
        fullText = notification.message;
      }
      fullText.trim();
      
      _instance->_callback(notification.app, fullText);
    }
  }
}