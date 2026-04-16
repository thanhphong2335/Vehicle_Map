#ifndef PARSER_H
#define PARSER_H

#include <Arduino.h>

// ============================================================
//  PARSER.H - Phân tích chuỗi notification từ Google Maps
//  Tác giả: ESP32 Navigation Project
//  Mô tả: Parse text navigation thành struct DirectionData
// ============================================================

// Enum cho các hướng rẽ
enum DirectionType {
  DIR_UNKNOWN   = 0,
  DIR_STRAIGHT  = 1,   // Đi thẳng
  DIR_LEFT      = 2,   // Rẽ trái
  DIR_RIGHT     = 3,   // Rẽ phải
  DIR_UTURN     = 4,   // Quay đầu
  DIR_SLIGHT_L  = 5,   // Rẽ trái nhẹ
  DIR_SLIGHT_R  = 6,   // Rẽ phải nhẹ
  DIR_SHARP_L   = 7,   // Rẽ trái gấp
  DIR_SHARP_R   = 8,   // Rẽ phải gấp
  DIR_ARRIVE    = 9,   // Đến nơi
  DIR_ROUNDABOUT= 10   // Vòng xuyến
};

// Struct lưu dữ liệu điều hướng đã được parse
struct DirectionData {
  DirectionType direction;  // Hướng rẽ (enum)
  String        dirText;    // Tên hướng (ví dụ: "Turn Right")
  int           distance;   // Khoảng cách tính bằng mét
  String        distText;   // Chuỗi khoảng cách gốc (ví dụ: "200 m", "1.2 km")
  String        streetName; // Tên đường (nếu có)
  bool          isValid;    // Dữ liệu hợp lệ hay không
};

// ============================================================
//  Class Parser - Xử lý chuỗi notification
// ============================================================
class NavigationParser {
public:
  NavigationParser();
  
  // Hàm chính: Parse một chuỗi notification
  // Input:  "Turn right in 200 m" hoặc "Turn left onto Nguyen Trai in 50 m"
  // Output: struct DirectionData đã điền đầy đủ
  DirectionData parse(const String& notification);
  
  // Lấy DirectionData hiện tại
  DirectionData getCurrentDirection() const { return _current; }
  
  // Kiểm tra có dữ liệu hợp lệ không
  bool hasValidData() const { return _current.isValid; }
  
  // Reset dữ liệu
  void reset();
  
private:
  DirectionData _current;
  
  // Parse hướng rẽ từ chuỗi (lowercase)
  DirectionType parseDirection(const String& text);
  
  // Parse khoảng cách (trả về số mét)
  // Ví dụ: "200 m" -> 200, "1.2 km" -> 1200, "0.5 km" -> 500
  int parseDistance(const String& text);
  
  // Trích xuất tên đường (nếu có từ khóa "onto" hoặc "on")
  String parseStreetName(const String& text);
  
  // Chuẩn hóa string về lowercase và trim
  String normalize(const String& text);
  
  // Tìm số trong chuỗi con
  float extractNumber(const String& text, int startPos);
};

#endif // PARSER_H
