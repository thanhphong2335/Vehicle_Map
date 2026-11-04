# MotoMap S3 — ESP32-S3 N16R8 Edition
*Heading-Up Navigation, Offline Maps, Fall Detection & Emergency Zalo SOS*  
*Hệ thống dẫn đường thông minh, bản đồ Offline, cảm biến té ngã & Cảnh báo SOS Zalo*

---

## 🌐 Language / Ngôn ngữ
- [English](#-english)
- [Tiếng Việt](#-tiếng-việt)

---

# 🇬🇧 English

## 1. Overview
The **MotoMap S3 Edition** (`n16r8_ver`) is an open-source, automotive-grade navigation and rider safety device engineered for the **ESP32-S3 DevKitC-1 N16R8** microcontroller (**16 MB Quad-SPI Flash, 8 MB Octal-SPI PSRAM**).

Equipped with an onboard GPS receiver, a 6-DoF inertial measurement unit (IMU), an SPI color IPS display, and dual Wi-Fi (AP + Station), MotoMap provides true standalone heading-up turn-by-turn navigation without requiring continuous phone screen operation or high data usage.

```
       +-----------------------------------------------------------+
       |                     M O T O M A P  S 3                    |
       |                                                           |
       |  [NEO-6M GPS] --(UART)--> [ESP32-S3 Dual-Core 240MHz]     |
       |  [MPU-6050 IMU] -(I2C)-->       |       |                 |
       |                                 |       |-(DMA SPI)--> [ST7789 IPS Display]
       |  [Phone Hotspot] <--(Wi-Fi)-----+       |
       |         |                               +-(LittleFS)-> [Web UI & Offline Tiles]
       |         v
       |  [Vercel Serverless Relay]
       |         v
       |  [Cloudflare Worker + KV]
       |         v
       |  [Zalo Bot Platform] ----> [Emergency Relatives' Zalo Chats]
       +-----------------------------------------------------------+
```

---

## 2. Core Features

1. **Heading-Up Navigation:**  
   Smooth real-time rotating map and route canvas aligned with vehicle heading (compass or GPS track). The navigation arrow stays anchored at 12 o'clock while the map rotates fluidly.
2. **Double-Buffering in 8 MB Octal PSRAM:**  
   A 400×400 canvas rendered entirely in PSRAM via DMA SPI at 240 MHz CPU clock. Zero screen flicker, zero Flash degradation.
3. **Proactive Route Tile Pre-Fetching & Offline Maps:**  
   Background worker pre-caches map tiles along upcoming route segments into 13.25 MB LittleFS storage with 3-tier memory caching for uninterrupted navigation in dead zones.
4. **Autonomous Fall & Crash Detection (FreeRTOS Core 0):**  
   Continuous 6-DoF motion tracking. If vehicle tilt (Roll/Pitch) exceeds 45° continuously for 5 seconds, an emergency state is automatically latched and dispatched.
5. **Automated Emergency SOS via Zalo Bot:**  
   Automatically packages GPS coordinates, heading, speed, and Google Maps location into a clear Vietnamese alert message sent to emergency contacts via Cloudflare Gateway & Vercel Relay.
6. **Modern Collapsible Web Dashboard:**  
   Responsive mobile/desktop web portal featuring collapsible navigation sidebar, destination search, live tilt visualization, offline tile downloads, and Developer Mode partition.
7. **Standby Screen & Dynamic QR Code:**  
   Displays an enlarged high-contrast QR code for instant smartphone connection when connected to Wi-Fi/4G. Reverts to the clean AP standby screen as soon as connection is lost.

---

## 3. Hardware Pinout & Wiring

| Peripheral | Pin | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- | :--- |
| **ST7789 IPS Display** | SCL / SCK | **GPIO 13** | SPI Clock (FSPI) |
| | SDA / MOSI | **GPIO 12** | SPI Data (FSPI) |
| | RST / RES | **GPIO 11** | Hardware Reset |
| | DC / A0 | **GPIO 10** | Data / Command Selection |
| | CS | **GPIO 9** | Chip Select (CS) |
| | VCC / GND | 3V3 / GND | Power & Ground |
| **NEO-6M GPS Module** | TX | **GPIO 17** | GPS TX -> ESP32-S3 RX (UART1) |
| | RX | **GPIO 18** | GPS RX <- ESP32-S3 TX (UART1) |
| | VCC / GND | 3V3 (or 5V) / GND | Power & Ground |
| **MPU-6050 IMU** | SDA | **GPIO 8** | I2C Data (SDA) |
| | SCL | **GPIO 7** | I2C Clock (SCL) |
| | VCC / GND | 3V3 / GND | Power & Ground |
| **BOOT Button** | BOOT / KEY | **GPIO 0** | Onboard Button (Active LOW) |

---

## 4. Build and Installation

### Prerequisites
- [PlatformIO Core (CLI)](https://platformio.org/install/cli) or VS Code with PlatformIO IDE extension.
- Python 3.8+.

### Quick Build Script (`build.cmd`)
```cmd
cd n16r8_ver

:: 1. Flash firmware (automatically compiles if changed)
.\build.cmd upload

:: 2. Upload Web UI & assets to LittleFS (only needed when modifying files in data/)
.\build.cmd uploadfs

:: 3. Monitor serial logs
.\build.cmd monitor
```

*(Note: Close the Serial Monitor before running `upload` or `uploadfs` to prevent COM port conflicts).*

---

## 5. Web Dashboard Architecture

The Web Server runs locally on ESP32-S3 with a dark-themed responsive interface:
- **Collapsible Sidebar:** Tap the hamburger icon on top-left to toggle between:
  - 🗺️ **Home & Maps:** Hotspot Wi-Fi config, Destination search, Leaflet Map with Retina 512px tiles, turn-by-turn status, offline map downloader, and real-time IMU visualizer.
  - ⚙️ **Settings:** Zalo SOS alert configuration, recipient phone numbers, and Developer Mode toggle.
- **Developer Mode Switch:**
  - *Standard Mode (Default):* Clean user interface showing only recipient phone numbers and the Zalo alert activation toggle.
  - *Developer Mode (Active):* Unlocks advanced fields for Cloudflare Gateway URL, Bearer Auth Token, and quick API endpoint diagnostic links (`/api/status`, `/api/imu`, `/api/alert-config`).

---

## 6. Zalo SOS Alert Pipeline Setup

### Option 1: Instant Setup (Using Pre-Deployed Gateway)
1. **Link Recipient Phone Number:**  
   Open your Zalo Bot and send:
   ```text
   DK 0901234567
   ```
   The bot will confirm successful linkage.
2. **Configure on Device:**  
   Open the Web Dashboard (`http://192.168.4.1/` -> Settings), enter the registered phone number, and toggle Zalo alert ON.
3. **Test:**  
   Click **Send Test Alert** in Settings or tilt the bike > 45° to verify delivery.

### Option 2: Self-Hosting (Optional for Developers)
- **Cloudflare Worker:** `cd tools/cloudflare_alert_worker && npx wrangler deploy`
- **Vercel Relay:** `cd tools/zalo_relay_vercel && npx vercel --prod`
- **Webhook Registration:** Access `https://<WORKER_DOMAIN>/api/set-webhook` to link with Zalo Bot Platform.

---

# 🇻🇳 Tiếng Việt

## 1. Giới thiệu tổng quan
**MotoMap S3 Edition** (`n16r8_ver`) là hệ thống dẫn đường độc lập và cảnh báo an toàn thông minh dành riêng cho xe máy, chạy trên vi điều khiển **ESP32-S3 DevKitC-1 N16R8** (**16 MB Quad-SPI Flash, 8 MB Octal-SPI PSRAM**).

Thiết bị tích hợp module định vị vệ tinh GPS, cảm biến đo chuyển động và góc nghiêng 6 trục (IMU), màn hình màu IPS chống lóa, cùng kết nối Wi-Fi kép (vừa phát AP vừa kết nối Hotspot điện thoại), mang đến trải nghiệm dẫn đường từng khúc cua xoay theo hướng xe (Heading-Up) hoàn toàn tự chủ mà không làm nóng máy hay hao pin điện thoại.

---

## 2. Tính năng nổi bật

1. **Bản đồ xoay theo hướng xe (Heading-Up Navigation):**  
   Bản đồ và lộ trình OSRM tự động xoay mượt mà theo góc la bàn / GPS của xe. Mũi tên dẫn đường cố định ở góc 12 giờ.
2. **Double-Buffering 100% trong 8 MB PSRAM:**  
   Canvas 400×400 xử lý và xoay hoàn toàn trong bộ nhớ Octal PSRAM qua kênh DMA SPI tốc độ 240 MHz. Không giật hình, không hao mòn bộ nhớ Flash.
3. **Tải trước bản đồ theo lộ trình (Pre-Fetching) & Bản đồ Offline:**  
   Tác vụ nền tự động tải trước các mảnh bản đồ dọc theo cung đường sắp đi và lưu vào phân vùng LittleFS 13.25 MB, giúp dẫn đường liên tục ngay cả khi vào vùng mất sóng 4G.
4. **Phát hiện té ngã tự động (IMU FreeRTOS Core 0):**  
   Cảm biến MPU-6050 liên tục theo dõi góc nghiêng Roll/Pitch. Khi xe nghiêng quá 45° trong 5 giây, hệ thống tự động xác nhận sự cố tai nạn.
5. **Tự động gửi cảnh báo cứu hộ qua Zalo Bot:**  
   Đóng gói toạ độ GPS, vận tốc, hướng di chuyển và tự động gửi tin nhắn tiếng Việt kèm link Google Maps trực tiếp đến Zalo của người thân qua hệ thống Cloudflare Gateway & Vercel Relay.
6. **Giao diện Web Dashboard thanh bên có thể thu gọn:**  
   Giao diện Dark Theme hiện đại, có thanh bên (Sidebar) chuyển tab Trang chủ / Cài đặt, bản đồ Retina sắc nét, bộ hiển thị góc nghiêng xe 3D và phân quyền Chế độ Nhà phát triển.
7. **Màn hình chờ thông minh & Mã QR tự động:**  
   Khi có mạng Wi-Fi/4G, màn hình hiển thị mã QR phóng to để điện thoại quét kết nối nhanh. Khi ngắt 4G hoặc mất kết nối, màn hình lập tức hạ về giao diện chờ ban đầu không mã QR.

---

## 3. Sơ đồ đấu nối chân phần cứng (ESP32-S3 N16R8)

| Ngoại vi | Chân linh kiện | GPIO ESP32-S3 | Chức năng |
| :--- | :--- | :--- | :--- |
| **Màn hình IPS ST7789** | SCL / SCK | **GPIO 13** | Xung nhịp SPI (FSPI Clock) |
| | SDA / MOSI | **GPIO 12** | Dữ liệu SPI (FSPI MOSI) |
| | RST / RES | **GPIO 11** | Reset phần cứng LCD |
| | DC / A0 | **GPIO 10** | Chọn Data / Command |
| | CS | **GPIO 9** | Chip Select (CS) |
| | VCC / GND | 3V3 / GND | Nguồn & Mass |
| **Module GPS NEO-6M** | TX | **GPIO 17** | GPS TX -> ESP32-S3 RX (UART1) |
| | RX | **GPIO 18** | GPS RX <- ESP32-S3 TX (UART1) |
| | VCC / GND | 3V3 (hoặc 5V) / GND | Nguồn & Mass |
| **Cảm biến IMU MPU-6050** | SDA | **GPIO 8** | Dữ liệu I2C (SDA) |
| | SCL | **GPIO 7** | Xung nhịp I2C (SCL) |
| | VCC / GND | 3V3 / GND | Nguồn & Mass |
| **Nút nhấn BOOT** | BOOT / KEY | **GPIO 0** | Nút bấm tích hợp trên mạch |

---

## 4. Hướng dẫn biên dịch và Nạp chương trình

Thực hiện trong thư mục `n16r8_ver` bằng script tiện ích `build.cmd`:

```cmd
cd n16r8_ver

:: 1. Nạp firmware vào ESP32-S3 (tự động biên dịch nếu có thay đổi)
.\build.cmd upload

:: 2. Nạp giao diện Web & tài nguyên vào phân vùng LittleFS (chỉ cần chạy khi sửa file data/)
.\build.cmd uploadfs

:: 3. Mở Serial Monitor theo dõi thông tin
.\build.cmd monitor
```

*(Lưu ý: Luôn đóng Serial Monitor trước khi chạy `upload` hoặc `uploadfs` để tránh lỗi tranh chấp cổng COM).*

---

## 5. Hướng dẫn sử dụng Web Dashboard

1. **Kết nối thiết bị:**
   - Kết nối điện thoại vào Wi-Fi do ESP32 phát: `MotoMap-ESP32` (Mật khẩu: `motomap123`) hoặc quét mã QR trên màn hình.
   - Mở trình duyệt truy cập: `http://192.168.4.1/`.
2. **Thanh bên điều hướng (Collapsible Sidebar):**
   - Bấm vào biểu tượng 3 dấu gạch ở góc trên bên trái để mở thanh bên.
   - **Trang chủ & Bản đồ:** Nhập Wi-Fi hotspot của điện thoại, tìm kiếm điểm đến, xem lộ trình và đồ họa góc nghiêng xe máy thời gian thực.
   - **Cài đặt (Settings):** Nhập số điện thoại người nhận cứu hộ Zalo, kích hoạt công tắc cảnh báo.
3. **Chế độ Nhà phát triển (Developer Mode):**
   - Mặc định ở chế độ người dùng: ẩn toàn bộ các URL máy chủ Cloudflare và Auth Token để tránh bấm nhầm.
   - Khi bật công tắc **Chế độ Nhà phát triển**: hiển thị đầy đủ Cloudflare Gateway URL, Gateway Auth Token và bảng liên kết kiểm tra nhanh các API hệ thống (`/api/status`, `/api/imu`, `/api/alert-config`).

---

## 6. Thiết lập Hệ thống Cảnh báo SOS qua Zalo Bot

### Cách 1: Sử dụng nhanh (Hệ thống đã triển khai sẵn)
1. **Liên kết số điện thoại người nhận:**  
   Người thân mở Zalo Bot (`Bot MotoMap SOS`), nhắn tin theo cú pháp:
   ```text
   DK 0901234567
   ```
   Bot sẽ phản hồi xác nhận liên kết thành công.
2. **Cấu hình trên thiết bị:**  
   Mở Web Dashboard (`http://192.168.4.1/` -> Cài đặt), nhập số điện thoại trên vào ô danh sách và gạt kích hoạt cảnh báo Zalo.
3. **Kiểm tra thử nghiệm:**  
   Nhấn nút **Gửi thử nghiệm** trên web hoặc nghiêng xe quá 45° trong 5 giây.

### Cách 2: Tự triển khai Gateway riêng (Tùy chọn cho Nhà phát triển)
- **Cloudflare Worker:** `cd tools/cloudflare_alert_worker && npx wrangler deploy`
- **Vercel Relay:** `cd tools/zalo_relay_vercel && npx vercel --prod`
- **Đăng ký Webhook:** Mở `https://<WORKER_DOMAIN>/api/set-webhook` trên trình duyệt để tự động kích hoạt Webhook Zalo.

---

## 7. Cấu trúc thư mục mã nguồn

```
n16r8_ver/
├── data/                             # Giao diện Webserver & Phân vùng LittleFS
│   ├── app.js                        # Logic xử lý giao diện, bản đồ, IMU, settings
│   ├── index.html                    # Cấu trúc HTML responsive với Sidebar & Tab
│   ├── styles.css                    # CSS Dark theme đồng bộ, switch toggle
│   └── ui/setup_background.jpg       # Ảnh nền màn hình chờ Cyberneon (320x240)
├── include/                          # Header files C++
│   ├── AlertManager.h                # Quản lý hàng đợi và tác vụ gửi cảnh báo SOS
│   ├── ConnectionManager.h           # Wi-Fi Station, SoftAP, WebServer, Captive Portal
│   ├── DebugLog.h                    # Hệ thống ghi log (tắt mặc định, 0% RAM/ROM)
│   ├── DisplayConfig.h               # Thông số phần cứng màn hình LCD ST7789
│   ├── DisplayManager.h              # Quản lý đồ họa LovyanGFX, mã QR, màn hình chờ
│   ├── ImuManager.h                  # Đọc cảm biến MPU-6050, thuật toán phát hiện té ngã
│   ├── NavigationMapManager.h        # Bộ dựng bản đồ xoay Heading-Up trong PSRAM
│   ├── PinConfig.h                   # Cấu hình chân GPIO phần cứng an toàn
│   └── RouteManager.h                # Quản lý lộ trình dẫn đường OSRM
├── src/                              # Source code C++ (.cpp)
├── tools/
│   ├── cloudflare_alert_worker/      # Cloudflare Worker Gateway + KV Database
│   └── zalo_relay_vercel/            # Vercel Serverless Webhook Relay
├── build.cmd                         # Script Windows tự động build, upload, monitor
└── platformio.ini                    # Cấu hình môi trường biên dịch PlatformIO
```
