# ESP32 Navigation Display 🧭
Hiển thị điều hướng Google Maps lên màn hình OLED thông qua Bluetooth (Chronos App), tối ưu hóa cho xe máy/xe đạp.

✨ **Phiên bản mới nhất:** Tích hợp giao diện Pixel-Perfect Bitmap, chống nhấp nháy (Flicker-free) và hỗ trợ hoàn chỉnh Tiếng Việt (chuẩn hóa UTF-8).

---

## 🌟 TÍNH NĂNG NỔI BẬT

- **Giao diện Minimalist & Chuyên nghiệp:** Sử dụng bộ icon chỉ đường Bitmap 24x24 pixels sắc nét thay vì các nét vẽ thô sơ.
- **Mượt mà (Flicker-Free):** Render qua bộ đệm (RAM) của thư viện U8g2, tần số quét I2C 400kHz giúp màn hình không bao giờ bị chớp giật.
- **Parser Tiếng Việt Thông Minh:** Tự động loại bỏ dấu tiếng Việt, xử lý hoàn hảo các ký tự đặc biệt như "Đ/đ", tự động suy luận hướng đi thẳng nếu Maps chỉ báo khoảng cách.
- **Hiệu ứng Animation:** Mũi tên trượt nhẹ nhàng tạo cảm giác đang di chuyển và chớp tắt (blinking) cảnh báo khi sắp đến điểm rẽ (< 50m).
- **Tiêu thụ pin thấp:** Sử dụng BLE (NimBLE) và OLED monochrome tối ưu cho các hệ thống nhúng IoT.

---

## 📦 CẤU TRÚC PROJECT

```text
ESP32_Navigation/
├── ESP32_Navigation.ino        ← File chính (Vòng lặp & Setup hệ thống)
├── src/
│   ├── parser.h/.cpp           ← Logic phân tích notification Maps (Anh/Việt)
│   ├── display_handler.h/.cpp  ← Điều khiển OLED, layout giao diện & Animation
│   ├── bluetooth_handler.h/.cpp← Xử lý BLE với app Chronos
│   └── icons.h                 ← [MỚI] Bộ icon XBM 24x24 (Rẽ, Đi thẳng, Quay đầu...)
└── README.md
```

---

## 🔧 PHẦN CỨNG & NỐI DÂY

### Linh kiện cần có:
| Linh kiện | Ghi chú |
|-----------|---------|
| ESP32 DevKit | Nên dùng bản có chip nạp CH340C/CP2102 |
| OLED 1.3" SH1106 I2C | Màn hình I2C (128x64 pixel), địa chỉ thường là 0x3C |


### Kiểm tra I2C address:
Nếu OLED không hiện, chạy I2C scanner để tìm địa chỉ:
- SH1106 thường là `0x3C` hoặc `0x3D`
- Xem file `i2c_scanner.ino` ở cuối README

---

## 📚 CÀI THƯ VIỆN (Arduino IDE)

1. Mở Arduino IDE
2. Vào **Tools → Manage Libraries** (Ctrl+Shift+I)
3. Tìm và cài:

| Tên thư viện | Tác giả | Version |
|--------------|---------|---------|
| **U8g2** | oliver | ≥ 2.35.30 |
| **NimBLE-Arduino** | h2zero | ≥ 1.4.0 |
| **ChronosESP32** | fbiego | 1.9.0 |

---

## ⚙️ CẤU HÌNH ARDUINO IDE

### Chọn Board ESP32:
1. **File → Preferences → Additional Board URLs**:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. **Tools → Board → Boards Manager** → Tìm "esp32" → Install "esp32 by Espressif Systems"
3. **Tools → Board → ESP32 Arduino → ESP32 Dev Module**

### Cài đặt Upload:
```
Board:          ESP32 Dev Module
Upload Speed:   115200
CPU Frequency:  240MHz
Flash Size:     4MB
Partition Scheme: Default 4MB with spiffs
Port:           COMx (Windows) hoặc /dev/ttyUSBx (Linux/Mac)
```

### Driver CH340C (nếu cần):
- Windows: https://www.wch.cn/downloads/CH341SER_EXE.html
- Mac: https://www.wch.cn/downloads/CH341SER_MAC_ZIP.html
- Linux: Đã có sẵn trong kernel

---

## 🚀 FLASH CODE

```
1. Kết nối ESP32 với máy tính qua cáp USB
2. Mở file ESP32_Navigation.ino trong Arduino IDE
3. Chọn đúng COM port (Tools → Port)
4. Nhấn Upload (→)
5. Khi thấy "Connecting..." trên màn hình IDE,
   giữ nút BOOT trên ESP32 ~1 giây rồi thả
   (một số board cần làm vậy để vào chế độ flash)
6. Đợi "Done uploading"
7. Mở Serial Monitor (115200 baud) để xem log
```

---

## 📱 CẤU HÌNH CHRONOS APP

### Bước 1: Tải Chronos
- **Android**: Tìm "Chronos" trên Google Play

### Bước 2: Kết nối ESP32
1. Mở Chronos app
2. Bật **Bluetooth**
3. Vào **Đồng hồ → Xem**
4. Tap **Ghép...**
5. Chọn **ESP32-NAV** trong danh sách

### Bước 3: Bật Notification cho Google Maps
1. Trong Chronos → **Notifications**
2. Bật tất cả notification **hoặc** chọn app cụ thể
3. Bật **Google Maps** (quan trọng!)
4. Bật **Notification Access** trong Android Settings

### Bước 4: Cấp quyền trên Android
```
Android Settings → Apps → Special App Access 
→ Notification Access → Chronos → Bật
```

### Bước 5: Test
1. Mở Google Maps
2. Tìm đường đến một địa điểm
3. Bắt đầu navigation
4. Màn hình OLED sẽ hiển thị hướng rẽ!

---

## 📡 GIAO THỨC BLUETOOTH

### Chronos sử dụng Nordic UART Service (NUS):
```
Service UUID:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
TX (Receive):  6E400002 (Phone writes to ESP32)
RX (Send):     6E400003 (ESP32 notifies Phone)
```

### Format dữ liệu nhận được:
```
Byte 0:    0xAB (Chronos magic header)
Byte 1:    Command (0x23 = notification)
Byte 2:    Length
Byte 3+:   JSON payload

Ví dụ (hex):
AB 23 3A 7B 22 61 70 70 22 3A 22 63 6F 6D 2E 67 6F 6F ...
```

### JSON payload ví dụ:
```json
{
  "app": "com.google.android.apps.maps",
  "title": "Navigation",
  "text": "Turn right in 200 m"
}
```

---

## 🧠 LOGIC PARSE NOTIFICATION

### Các chuỗi Google Maps hỗ trợ:

| Notification | Hệ thống phân tích | Hiển thị OLED |
|-------------|-----------|----------|
| `"0 m Đi về hướng Đông Bắc"` | DIR_STRAIGHT, 0m, "Dong Bac" | Mũi tên Thẳng + "Dong Bac" |
| `"Rẽ trái vào Lê Lợi trong 50 m"` | DIR_LEFT, 50m, "Le Loi" | Mũi tên Trái + "50 m" + "Le Loi" |
| `"0 m D. Bờ Nhà Thờ"` | DIR_STRAIGHT, 0m, "D. Bo Nha Tho" | Mũi tên Thẳng + "D. Bo Nha Tho" |

### Thuật toán parse:
```
1. Lowercase toàn bộ chuỗi
2. Loại bỏ toàn bộ dấu Tiếng Việt (Đ → d, á → a, ồ → o...).
3. Nhận diện từ khóa đa ngôn ngữ: re trai, turn left, di thang, straight, quay dau, u-turn.
```

---

## 🧪 TEST KHÔNG CẦN ĐIỆN THOẠI

Nhấn nút **BOOT** (GPIO0) trên ESP32 để cycle qua các scenario test:
```
Press 1: "Turn right in 200 m"
Press 2: "Turn left onto Nguyen Trai in 50 m"
Press 3: "Continue straight for 1.2 km"
Press 4: "Make a U-turn in 100 m"
Press 5: "Keep left at the fork in 300 m"
Press 6: "Slight right onto Le Loi in 80 m"
Press 7: "You have arrived"
Press 8: "Turn right in 15 m" ← Sẽ nhấp nháy!
```

---

## 🔍 TROUBLESHOOTING

### OLED không hiển thị:
```
✗ Kiểm tra dây nối SDA (GPIO21) và SCL (GPIO22)
✗ Kiểm tra nguồn 3.3V
✗ Chạy I2C Scanner để tìm địa chỉ thật
✗ Thử đổi địa chỉ OLED_I2C_ADDR từ 0x3C → 0x3D
```

### BLE không tìm thấy ESP32-NAV:
```
✗ Mở Serial Monitor kiểm tra log
✗ Tắt/mở Bluetooth điện thoại
✗ Xóa thiết bị cũ trong Settings BT điện thoại
✗ Restart ESP32
```

### Notification không nhận được:
```
✗ Kiểm tra Notification Access đã cấp cho Chronos chưa
✗ Trong Chronos, bật Notification cho Google Maps
✗ Thử test bằng nút BOOT trước
✗ Mở Serial Monitor để xem raw data BLE
```

### Upload lỗi "Failed to connect":
```
✗ Giữ nút BOOT khi thấy "Connecting..." trong IDE
✗ Kiểm tra driver CH340C
✗ Thử port COM khác
✗ Giảm Upload Speed xuống 115200
```

---

## 📎 I2C SCANNER (Tìm địa chỉ OLED)

```cpp
// i2c_scanner.ino - Chạy để tìm địa chỉ I2C của OLED
#include <Wire.h>
void setup() {
  Wire.begin(21, 22); // SDA, SCL
  Serial.begin(115200);
}
void loop() {
  for (int addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Found device at 0x%02X\n", addr);
    }
  }
  delay(3000);
}
```

---

## 📋 THÔNG SỐ KỸ THUẬT

| Thông số | Giá trị |
|---------|---------|
| Chip | ESP32 (Xtensa LX6 dual-core 240MHz) |
| Flash | 4MB |
| OLED Driver | SH1106 |
| Độ phân giải OLED | 128 × 64 pixels |
| Giao tiếp OLED | I2C @ 400kHz |
| Bluetooth | BLE 4.2 (NimBLE stack) |
| BLE Profile | Nordic UART Service (NUS) |
| Nguồn | 3.3V (ESP32) / 5V USB |
| Tiêu thụ điện | ~80mA (BLE active + OLED) |

---

*Project by ESP32 Navigation Team • Arduino IDE • NimBLE + U8g2*
