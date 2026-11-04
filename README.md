# MotoMap — Smart Motorcycle Navigation & Safety System
*Hệ thống dẫn đường thông minh & Cảnh báo an toàn cho xe máy*

---

## 🌐 Language / Ngôn ngữ
- [English](#-english)
- [Tiếng Việt](#-tiếng-việt)

---

# 🇬🇧 English

MotoMap is an open-source, automotive-grade motorcycle navigation and rider safety system powered by ESP32 microcontrollers. With an onboard GPS receiver, 6-axis IMU, and an SPI color display, MotoMap delivers standalone turn-by-turn navigation without requiring continuous smartphone screen usage.

```
       +-----------------------------------------------------------+
       |                     M O T O M A P                         |
       |                                                           |
       |  [GPS Module] ----(UART)----> [ESP32 Microcontroller]     |
       |  [IMU Sensor] -----(I2C)---->       |       |             |
       |                                     |       +-> [ST7789 IPS Display]
       |  [Smartphone Hotspot] <--(Wi-Fi)----+       |
       |         |                                   +-> [Web UI / LittleFS]
       |         v
       |  [Vercel Relay -> Cloudflare Worker]
       |         v
       |  [Zalo Bot Platform] ------> [Emergency Relatives' Zalo]
       +-----------------------------------------------------------+
```

### 🚀 Hardware Editions

1. **[ESP32-S3 N16R8 Edition (`n16r8_ver`)](n16r8_ver/README.md) *(Recommended / Flagship)***:
   - **Hardware:** ESP32-S3 DevKitC-1 (16 MB Flash, 8 MB Octal PSRAM).
   - **Display:** 2.0" / 1.69" ST7789 320×240 IPS.
   - **Navigation:** Heading-Up rotating map, 400×400 double-buffering in PSRAM via DMA SPI (zero flicker).
   - **Offline Storage:** 13.25 MB LittleFS for route pre-fetching and offline map caching.
   - **Safety:** FreeRTOS Core 0 fall detection (MPU-6050) & automated SOS dispatch to Zalo Bot via Cloudflare/Vercel.
   - **Web UI:** Responsive dark dashboard with collapsible sidebar and Developer Mode.

2. **[ESP32 Classic Edition (`moto_map`)](moto_map/)**:
   - **Hardware:** ESP32-WROOM-32 (4 MB Flash, No PSRAM).
   - **Display:** GMT147SPI ST7789 172×320.
   - **Navigation:** Lightweight turn-by-turn arrows and distance indicators.

### 📱 Smartphone Companion & Web Portal
- **Web Portal:** Connect your smartphone browser to the device Wi-Fi (`http://192.168.4.1/`) to select destinations, cache offline maps, and configure emergency contacts.
- **iOS Companion App:** Native Swift application located in [`ios/MotoMap`](ios/MotoMap/README.md).

---

# 🇻🇳 Tiếng Việt

MotoMap là hệ thống dẫn đường độc lập và cảnh báo an toàn thông minh dành cho xe máy trên nền tảng vi điều khiển ESP32. Kết hợp module định vị GPS, cảm biến IMU 6 trục và màn hình màu SPI độ sáng cao, MotoMap mang đến trải nghiệm dẫn đường từng khúc cua tự chủ mà không làm nóng hay hao pin điện thoại.

### 🚀 Các phiên bản phần cứng

1. **[Phiên bản ESP32-S3 N16R8 (`n16r8_ver`)](n16r8_ver/README.md) *(Khuyên dùng / Lá cờ đầu)***:
   - **Phần cứng:** ESP32-S3 DevKitC-1 (16 MB Flash, 8 MB Octal PSRAM).
   - **Màn hình:** ST7789 320×240 IPS (2.0" hoặc 1.69").
   - **Dẫn đường:** Bản đồ xoay theo hướng xe (Heading-Up), double-buffering 400×400 trong PSRAM qua DMA SPI (không giật hình).
   - **Bản đồ Offline:** Phân vùng LittleFS 13.25 MB tải trước và lưu trữ bản đồ dọc lộ trình.
   - **An toàn:** Cảm biến MPU-6050 phát hiện té ngã trên FreeRTOS Core 0 & tự động gửi định vị cứu hộ qua Zalo Bot.
   - **Web UI:** Giao diện Dark theme hiện đại, thanh bên thu gọn (Sidebar) và phân quyền Chế độ Nhà phát triển.

2. **[Phiên bản ESP32 Classic (`moto_map`)](moto_map/)**:
   - **Phần cứng:** ESP32-WROOM-32 (4 MB Flash, không PSRAM).
   - **Màn hình:** GMT147SPI ST7789 172×320.
   - **Dẫn đường:** Mũi tên chỉ hướng tinh gọn và thanh cự ly rẽ.

### 📱 Giao diện Web Portal & Ứng dụng điện thoại
- **Web Portal:** Mở trình duyệt trên điện thoại kết nối vào Wi-Fi của thiết bị (`http://192.168.4.1/`) để tìm kiếm điểm đến, tải bản đồ offline, theo dõi góc nghiêng xe và cài đặt số Zalo cứu hộ.
- **Ứng dụng iOS:** Ứng dụng Swift tương thích trong thư mục [`ios/MotoMap`](ios/MotoMap/README.md).
