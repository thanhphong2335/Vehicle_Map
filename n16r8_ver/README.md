# MotoMap S3 — Phiên bản ESP32-S3 N16R8 (Heading-Up Navigation)

Phiên bản nâng cấp dành riêng cho bo mạch **ESP32-S3 DevKit N16R8** (**16 MB Flash, 8 MB Octal PSRAM**).

---

## ✨ Điểm nâng cấp chính so với bản cũ (ESP32 Classic 4MB)

1. **Bản đồ xoay theo hướng di chuyển (Heading-Up Map):**
   * Bản đồ và đường tuyến OSRM tự động xoay mượt mà theo góc la bàn / hướng GPS của xe.
   * Mũi tên dẫn đường cố định ở góc 12 giờ (hướng nhìn thẳng về phía trước xe).
   * Giao diện tinh gọn, tập trung hoàn toàn vào tầm nhìn cung đường.
2. **Cập nhật tọa độ GPS tức thì (Dynamic Real-time Map):**
   * Loại bỏ hoàn toàn giới hạn đứng yên / tốc độ < 2 km/h của bản cũ.
   * Vị trí xe và bản đồ cập nhật ngay khi GPS trả fix mới (1Hz – 5Hz), không còn hiện tượng giữ lì khung hình.
3. **Double Buffering & Stream 100% trong 8MB PSRAM (Không ghi Flash, Không chớp nháy):**
   * Sử dụng 2 Sprite trong PSRAM: Canvas bản đồ xoay 400×400 và Framebuffer 320×240.
   * Toàn bộ quá trình vẽ tile, tuyến đường, mũi tên và thanh điều hướng đều được ghép ngầm trong PSRAM và đẩy ra màn hình qua SPI DMA trong 1 nhịp quét (Zero-flicker).
   * **Không ghi bản đồ vào Flash:** Các mảnh bản đồ PNG tải trực tiếp qua Wi-Fi hotspot từ điện thoại, giải mã thẳng vào bộ đệm 24 tile trong PSRAM (~3.0 MB). Tuyệt đối **không ghi/xóa Flash LittleFS** giúp bảo vệ tuổi thọ chip nhớ (Zero Flash Wear) và tải bản đồ siêu tốc.
4. **Tải trước bản đồ theo lộ trình (Proactive Route Tile Pre-Fetching):**
   * Quét trước dọc theo toàn bộ tuyến đường OSRM (lên đến 1000 điểm ~15-20 km phía trước xe).
   * Tác vụ ngầm tự động tải trước các tile của cung đường sắp đi qua và nạp vào 24 slot PSRAM cache.
   * Khi xe chạy tới ngã rẽ hay đoạn đường mới, bản đồ đã có sẵn trong PSRAM, hiển thị lập tức không có độ trễ.
   * Thuật toán LRU bảo vệ thông minh: Tuyệt đối không xóa các tile đang hiển thị trên màn hình hiện tại khi nạp tile mới.
5. **Video Animation 10s chuyển động siêu mượt 24 FPS (như xem trên điện thoại):**
   * Hỗ trợ chuẩn video 24 FPS (lên tới 240 frames cho video 10 giây) với độ phân giải đầy đủ 320×240.
   * **Nạp 100% vào Octal PSRAM:** Toàn bộ ~2.5 MB – 3.5 MB dữ liệu của 240 frames được nạp sẵn vào PSRAM. Khi phát, chip đọc trực tiếp từ Octal PSRAM tốc độ cao, loại bỏ hoàn toàn hiện tượng khựng/giật do đọc Flash, chuyển động trơn tru mượt mà như 1 video trên điện thoại.
6. **LittleFS dành trọn vẹn cho Web App & Video Media (~11.75 MB):**
   * Do bản đồ chạy trực tiếp trên RAM, LittleFS không bị chiếm dụng bởi cache bản đồ.
   * Toàn bộ dung lượng ~11.75 MB dành riêng cho giao diện web chọn điểm đến và lưu trữ video animation màn hình chờ.

---

## 🔌 Sơ đồ đấu dây chân (Pinout ESP32-S3)

> ⚠️ **LƯU Ý CỰC KỲ QUAN TRỌNG:**
> Trên ESP32-S3 bản N16R8, các chân **GPIO 26 đến GPIO 37** được chip sử dụng nội bộ cho đường truyền Flash và Octal PSRAM. **TUYỆT ĐỐI KHÔNG NỐI DÂY VÀO CÁC CHÂN NÀY.**

Cấu hình mặc định trong [include/PinConfig.h](include/PinConfig.h):

| Thiết bị | Chân màn / GPS | ESP32-S3 GPIO | Ghi chú |
| :--- | :--- | :--- | :--- |
| **Màn ST7789** | VCC | 3V3 / 5V | Nguồn màn hình |
| | GND | GND | Mass |
| | **SCL** / SCK | **GPIO 13** | SPI SCLK (FSPI) |
| | **SDA** / MOSI | **GPIO 12** | SPI MOSI (FSPI) |
| | **RST** / RES | **GPIO 11** | Reset màn hình |
| | **DC** / A0 / RS | **GPIO 10** | Data / Command |
| | **CS** | **GPIO 9** | Chip Select |
| **GPS NEO-6M** | VCC | 3V3 / 5V | Nguồn GPS |
| | GND | GND | Mass |
| | TX | **GPIO 17** | Nối vào chân RX của ESP32-S3 |
| | RX | **GPIO 18** | Nối vào chân TX của ESP32-S3 (tùy chọn) |
| **Nút bấm** | KEY / BOOT | **GPIO 0** | Nút BOOT có sẵn trên board ESP32-S3 |

*(Nếu bạn muốn đổi chân khác, chỉ cần sửa lại trong file `include/PinConfig.h` hoặc khai báo cờ `-DPIN_...` trong `platformio.ini`)*.

---

## 🛠️ Biên dịch và Nạp Code

Mở thư mục `n16r8_ver` trong VS Code hoặc PowerShell:

```powershell
cd "d:\Self-Project\Project ESP32 Moto_Map\n16r8_ver"

# Biên dịch firmware
.\build.cmd build

# Nạp firmware vào ESP32-S3
.\build.cmd upload

# Nạp giao diện Web / dữ liệu LittleFS (HTML, CSS, JS, ảnh)
.\build.cmd uploadfs

# Mở Serial Monitor xem log
.\build.cmd monitor
```

---

## 📋 Phân vùng bộ nhớ 16 MB (`partitions_16mb.csv` - Không OTA)

* **NVS:** 28 KB (`0x7000`)
* **Firmware App Factory:** 4 MB (`0x400000` = 4,194,304 bytes)
* **LittleFS File System:** ~11.75 MB (`0xBC0000` = 12,320,768 bytes)
* **CoreDump:** 192 KB (`0x30000`)

---

## 🎬 Tạo video 10s 24 FPS mượt mà cho ESP32-S3

Bạn có 2 cách đưa video vào thiết bị:

### Cách 1: Nạp từ máy tính qua PowerShell (Chất lượng sắc nét nhất)
Chạy script tự động cắt frame và nén bằng ffmpeg:
```powershell
powershell -ExecutionPolicy Bypass -File tools/video_to_frames.ps1 -InputFile "duong_dan_video.mp4" -Fps 24 -Seconds 10 -Width 320 -Height 240
```
Sau đó nạp vào bộ nhớ LittleFS của ESP32-S3:
```powershell
.\build.cmd uploadfs
```

### Cách 2: Tải trực tiếp qua Web App trên điện thoại
1. Kết nối điện thoại vào Wi-Fi của MotoMap (hoặc truy cập IP của thiết bị khi kết nối hotspot).
2. Mở trình duyệt vào giao diện MotoMap, kéo xuống phần **MEDIA**.
3. Chọn video 10s từ album điện thoại, nhấn **Lưu làm Animation**.
4. Trình duyệt sẽ tự động render các frame ở 24 FPS và gửi qua ESP32-S3. ESP32-S3 sẽ lưu vào LittleFS và tự động nạp toàn bộ vào PSRAM để phát mượt mà như xem trên điện thoại.

