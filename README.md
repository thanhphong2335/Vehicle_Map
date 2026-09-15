# MotoMap

MotoMap là thiết bị chỉ đường độc lập dùng ESP32 DevKit/WROOM-32, GPS GY-NEO6MV2 và màn GMT147SPI ST7789 172×320 đặt ngang. iPhone chỉ dùng để chọn điểm đến và chia sẻ Internet; sau khi xác nhận có thể đóng Safari và khóa màn hình.

## Đấu dây

| Thiết bị | ESP32 |
| --- | --- |
| Màn VCC | 3V3 |
| Màn GND | GND |
| Màn SCK/CLK | GPIO18 |
| Màn MOSI/SDA | GPIO23 |
| Màn CS | GPIO5 |
| Màn DC/A0 | GPIO25 |
| Màn RST/RES | GPIO17 |
| Màn BL/LED | GPIO32 |
| GPS VCC | 3V3 |
| GPS GND | GND |
| GPS TX | GPIO26 (ESP32 RX2) |
| GPS RX | GPIO27 (ESP32 TX2, tùy chọn) |

GPIO4 và GPIO5 không được firmware sử dụng.

## Build và nạp

Mở đúng thư mục `D:\Project ESP32 Moto_Map\moto_map` trong VS Code/PlatformIO rồi chạy:

```powershell
platformio run
platformio run --target upload
platformio run --target uploadfs
platformio device monitor --baud 115200
```

Phải nạp cả firmware lẫn filesystem. Project dùng partition 4 MB không OTA: app 2 MB, LittleFS khoảng 1,875 MB và coredump 64 KB.

## Sử dụng lần đầu

1. Bật **Dữ liệu di động**, **Điểm truy cập cá nhân**, **Cho phép người khác kết nối** và **Tối đa hóa tương thích** trên iPhone.
2. ESP32 thử nối hotspot đã lưu trong 20 giây. Nếu chưa có cấu hình, nó phát Wi-Fi `MotoMap-ESP32`, mật khẩu `motomap123`.
3. Kết nối iPhone vào Wi-Fi đó và mở `http://192.168.4.1/`.
4. Nhập tên/mật khẩu hotspot iPhone, tìm địa chỉ hoặc ghim bản đồ, rồi bấm **Xác nhận & bắt đầu**.
5. ESP32 lưu cấu hình, chuyển sang hotspot sau 5 giây và hiện IP mới trên màn hình.
6. Các lần sau mở `http://motomap.local/` hoặc IP đang hiện trên màn hình để chọn điểm đến mới.

Sau khi xác nhận, GPS và routing chạy trên ESP32. Safari có thể đóng. Khi lệch tuyến trên 60 m trong ba lần GPS liên tiếp, ESP32 tự gọi OSRM để tính lại.

Nếu hotspot mất, ESP32 tiếp tục tuyến đang có trong RAM. Khi đang lệch mà mất mạng, màn hình báo chờ và tự reroute sau khi hotspot trở lại.

## HTTP API

```text
GET  /api/status
GET  /api/config
POST /api/config/wifi
POST /api/destination
POST /api/navigation/stop
POST /api/navigation/reroute

POST   /api/media/animation/begin
POST   /api/media/animation/frame
POST   /api/media/animation/commit
DELETE /api/media/animation
```

Payload điểm đến:

```json
{
  "lat": 10.8231,
  "lon": 106.6297,
  "label": "Điểm đã ghim",
  "vehicle": "motorcycle",
  "start": true
}
```

Các endpoint cũ `/status`, `/navigation/stop`, `/mode/media`, `/mode/idle` và `/media/frame` vẫn được giữ để tương thích.

## Animation JPEG 10 giây

Web có thể chọn video và lưu 60 frame JPEG 320×172, 6 fps vào LittleFS. Animation tự lặp khi không navigation và tự dừng khi navigation bắt đầu.

Để nạp video cùng filesystem bằng máy tính, cần FFmpeg:

```powershell
cd D:\Project ESP32 Moto_Map\moto_map
.\tools\video_to_frames.ps1 -InputFile "D:\video\intro.mp4"
platformio run --target uploadfs
```

Script tự giảm chất lượng cho tới khi tổng animation không quá 1,45 MB và mỗi frame không quá 32 KB. Chỉ có một animation tại một thời điểm; upload mới sẽ thay animation cũ.

## Giới hạn

- GPS cần nhìn thấy bầu trời; anten không nên bị kim loại che.
- Public OSRM/Nominatim phù hợp prototype cá nhân, không phải dịch vụ production.
- Profile public OSRM hiện là `driving` cho cả xe máy và ô tô.
- Route nằm trong RAM; điểm đến/hotspot nằm trong NVS. Sau reboot, ESP32 xin route mới từ vị trí GPS hiện tại.
- HTTPS tới OSRM đang dùng kết nối mã hóa nhưng chưa xác minh CA (`setInsecure`) cho bản prototype.
