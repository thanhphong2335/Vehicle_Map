#ifndef BLUETOOTH_HANDLER_H
#define BLUETOOTH_HANDLER_H

#include <Arduino.h>
#include <ChronosESP32.h> // Gọi thư viện mới

// Callback khi nhận được notification
typedef void (*NotificationCallback)(const String& appName, const String& notificationText);

class BluetoothHandler {
public:
  BluetoothHandler();
  
  void begin();
  void tick();
  bool isConnected() const;
  void onNotificationReceived(NotificationCallback cb);
  String getLastAppName() const { return _lastAppName; }

private:
  ChronosESP32 _chronos;
  bool _connected;
  String _lastAppName;
  NotificationCallback _callback;

  // C++ yêu cầu hàm static để truyền vào callback của thư viện ngoài
  static BluetoothHandler* _instance;
  static void onConnectionChange(bool connected);
  static void onNotify(Notification notification);
};

#endif // BLUETOOTH_HANDLER_H