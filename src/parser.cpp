#include "parser.h"

// ============================================================
//  PARSER.CPP - Triển khai logic parse notification
// ============================================================

NavigationParser::NavigationParser() {
  reset();
}

void NavigationParser::reset() {
  _current.direction  = DIR_UNKNOWN;
  _current.dirText    = "Unknown";
  _current.distance   = 0;
  _current.distText   = "";
  _current.streetName = "";
  _current.isValid    = false;
}

// ============================================================
//  HÀM CHÍNH: Parse toàn bộ chuỗi notification
//  Ví dụ input:
//    "Turn right in 200 m"
//    "Turn left onto Nguyen Trai in 50 m"
//    "Continue straight for 1.2 km"
//    "Make a U-turn in 100 m"
//    "You have arrived at your destination"
// ============================================================
DirectionData NavigationParser::parse(const String& notification) {
  reset();
  
  if (notification.length() == 0) {
    return _current;
  }
  
  String lower = normalize(notification);
  Serial.println("[PARSER] Parsing: " + notification);
  
  // 1. Parse hướng rẽ
  _current.direction = parseDirection(lower);
  
  // 2. Gán tên hướng dạng text
  switch (_current.direction) {
    case DIR_STRAIGHT:   _current.dirText = "Straight";    break;
    case DIR_LEFT:       _current.dirText = "Turn Left";   break;
    case DIR_RIGHT:      _current.dirText = "Turn Right";  break;
    case DIR_UTURN:      _current.dirText = "U-Turn";      break;
    case DIR_SLIGHT_L:   _current.dirText = "Slight Left"; break;
    case DIR_SLIGHT_R:   _current.dirText = "Slight Right";break;
    case DIR_SHARP_L:    _current.dirText = "Sharp Left";  break;
    case DIR_SHARP_R:    _current.dirText = "Sharp Right"; break;
    case DIR_ARRIVE:     _current.dirText = "Arrived!";    break;
    case DIR_ROUNDABOUT: _current.dirText = "Roundabout";  break;
    default:             _current.dirText = "Unknown";     break;
  }
  
  // 3. Parse khoảng cách
  _current.distance = parseDistance(lower);
  
  // 4. CHỈNH SỬA: Tạo chuỗi khoảng cách hiển thị (Chấp nhận khoảng cách 0m)
  if (_current.distance >= 1000) {
    float km = _current.distance / 1000.0f;
    char buf[16];
    if (km == (int)km) {
      snprintf(buf, sizeof(buf), "%d km", (int)km);
    } else {
      snprintf(buf, sizeof(buf), "%.1f km", km);
    }
    _current.distText = String(buf);
  } else if (_current.distance >= 0 && (lower.indexOf(" m") > 0 || lower.indexOf(" km") > 0)) { 
    // Yêu cầu có chữ 'm' hoặc 'km' trong chuỗi để tránh in 0m cho các tin rác
    _current.distText = String(_current.distance) + " m";
  } else {
    _current.distText = "";
  }
  
  // 5. Trích xuất tên đường
  _current.streetName = parseStreetName(lower); 
  
  // 6. CHỈNH SỬA: Logic mặc định cho việc đi thẳng
  // Nếu Maps không ghi rõ hướng, nhưng có thông báo khoảng cách (ví dụ "0 m D. Bờ Nhà Thờ") 
  // -> Mặc định là đang đi thẳng trên đường đó.
  if (_current.direction == DIR_UNKNOWN && lower.indexOf(" m") > 0) {
    _current.direction = DIR_STRAIGHT;
    _current.dirText = "Straight";
  }

  _current.isValid = (_current.direction != DIR_UNKNOWN);
  
  Serial.printf("[PARSER] Result: dir=%s, dist=%dm, street=%s\n",
    _current.dirText.c_str(),
    _current.distance,
    _current.streetName.c_str()
  );
  
  return _current;
}

// ============================================================
//  Parse hướng rẽ dựa trên từ khóa (Song ngữ Anh - Việt)
// ============================================================
DirectionType NavigationParser::parseDirection(const String& text) {
  // U-Turn (Quay đầu)
  if (text.indexOf("u-turn") >= 0 || text.indexOf("turn around") >= 0 || text.indexOf("quay dau") >= 0) {
    return DIR_UTURN;
  }
  // Đến nơi
  if (text.indexOf("arrived") >= 0 || text.indexOf("destination") >= 0 || text.indexOf("den noi") >= 0) {
    return DIR_ARRIVE;
  }
  // Slight left/right (Rẽ nhẹ / Chếch)
  if (text.indexOf("slight left") >= 0 || text.indexOf("bear left") >= 0 || text.indexOf("chech trai") >= 0) {
    return DIR_SLIGHT_L;
  }
  if (text.indexOf("slight right") >= 0 || text.indexOf("bear right") >= 0 || text.indexOf("chech phai") >= 0) {
    return DIR_SLIGHT_R;
  }
  // Sharp left/right (Rẽ gấp)
  if (text.indexOf("sharp left") >= 0 || text.indexOf("re gap trai") >= 0) {
    return DIR_SHARP_L;
  }
  if (text.indexOf("sharp right") >= 0 || text.indexOf("re gap phai") >= 0) {
    return DIR_SHARP_R;
  }
  // Turn left (Rẽ trái)
  if (text.indexOf("turn left") >= 0 || text.indexOf("left onto") >= 0 || text.indexOf("re trai") >= 0) {
    return DIR_LEFT;
  }
  // Turn right (Rẽ phải)
  if (text.indexOf("turn right") >= 0 || text.indexOf("right onto") >= 0 || text.indexOf("re phai") >= 0) {
    return DIR_RIGHT;
  }
  // Đi thẳng
  if (text.indexOf("straight") >= 0 || text.indexOf("continue") >= 0 || text.indexOf("di thang") >= 0 || text.indexOf("tiep tuc") >= 0 || text.indexOf("ve huong") >= 0) {
    return DIR_STRAIGHT;
  }
  // Vòng xuyến
  if (text.indexOf("roundabout") >= 0 || text.indexOf("rotary") >= 0 || text.indexOf("vong xuyen") >= 0) {
    return DIR_ROUNDABOUT;
  }
  
  return DIR_UNKNOWN;
}

// ============================================================
//  Parse khoảng cách - trả về số mét
//  Hỗ trợ: "200 m", "1.2 km", "500 ft", "0.3 miles"
// ============================================================
int NavigationParser::parseDistance(const String& text) {
  int kmPos = text.indexOf(" km");
  int mPos  = text.indexOf(" m");  
  int ftPos = text.indexOf(" ft");
  int miPos = text.indexOf(" mi"); 
  
  if (kmPos > 0) {
    float val = extractNumber(text, kmPos);
    if (val >= 0) { // Chỉnh sửa: Chấp nhận 0
      return (int)(val * 1000); 
    }
  }
  
  if (mPos > 0) {
    char after = (mPos + 2 < text.length()) ? text.charAt(mPos + 2) : ' ';
    if (after != 'i' && after != 'p') { 
      float val = extractNumber(text, mPos);
      if (val >= 0) { // Chỉnh sửa: Chấp nhận 0
        return (int)val;
      }
    }
  }
  
  if (ftPos > 0) {
    float val = extractNumber(text, ftPos);
    if (val > 0) return (int)(val * 0.3048f); 
  }
  
  if (miPos > 0) {
    float val = extractNumber(text, miPos);
    if (val > 0) return (int)(val * 1609.34f); 
  }
  
  return 0; 
}

// ============================================================
//  Trích xuất tên đường từ "onto", "on", "vào", "lên"
// ============================================================
String NavigationParser::parseStreetName(const String& text) {
  String result = "";
  int ontoPos = -1;
  
  String lower = normalize(text);
  
  if (lower.indexOf(" onto ") >= 0) {
    ontoPos = lower.indexOf(" onto ") + 6;
  } else if (lower.indexOf(" on ") >= 0 && lower.indexOf("turn") >= 0) {
    ontoPos = lower.indexOf(" on ") + 4;
  } else if (lower.indexOf(" vao ") >= 0) {      
    ontoPos = lower.indexOf(" vao ") + 5;
  } else if (lower.indexOf(" len ") >= 0) {      
    ontoPos = lower.indexOf(" len ") + 5;
  } else if (lower.indexOf(" ve huong ") >= 0) { 
    ontoPos = lower.indexOf(" ve huong ") + 10;
  } else if (lower.startsWith("ve huong ")) {    
    ontoPos = 9;
  }

  // CHỈNH SỬA: Thêm Fallback. Nếu không có từ khóa hướng, nhưng có cụm " m ", 
  // thì lấy phần tên đường nằm phía sau cụm " m ".
  if (ontoPos < 0) {
    int mSpacePos = lower.indexOf(" m ");
    if (mSpacePos > 0) {
      ontoPos = mSpacePos + 3;
    } else {
      return "";
    }
  }
  
  String afterOnto = lower.substring(ontoPos);
  
  int inPos = afterOnto.indexOf(" in ");
  if (inPos < 0) {
    inPos = afterOnto.indexOf(" sau ");
  }
  
  if (inPos > 0) {
    result = afterOnto.substring(0, inPos);
  } else {
    result = afterOnto;
  }
  
  result.trim();
  
  for (int i = 0; i < result.length(); i++) {
    if (i == 0 || result.charAt(i - 1) == ' ') {
      result.setCharAt(i, toupper(result.charAt(i)));
    }
  }
  
  return result;
}

// ============================================================
//  Chuẩn hóa chuỗi: lowercase + trim + loại bỏ dấu tiếng Việt
// ============================================================
String NavigationParser::normalize(const String& text) {
  String s = text;
  s.toLowerCase();
  s.trim();

  // Mảng các ký tự cần thay thế để bỏ dấu tiếng Việt
  const char* a_chars[] = {"à","á","ạ","ả","ã","â","ầ","ấ","ậ","ẩ","ẫ","ă","ằ","ắ","ặ","ẳ","ẵ"};
  for(int i=0; i<17; i++) s.replace(a_chars[i], "a");

  const char* e_chars[] = {"è","é","ẹ","ẻ","ẽ","ê","ề","ế","ệ","ể","ễ"};
  for(int i=0; i<11; i++) s.replace(e_chars[i], "e");

  const char* i_chars[] = {"ì","í","ị","ỉ","ĩ"};
  for(int i=0; i<5; i++) s.replace(i_chars[i], "i");

  const char* o_chars[] = {"ò","ó","ọ","ỏ","õ","ô","ồ","ố","ộ","ổ","ỗ","ơ","ờ","ớ","ợ","ở","ỡ"};
  for(int i=0; i<17; i++) s.replace(o_chars[i], "o");

  const char* u_chars[] = {"ù","ú","ụ","ủ","ũ","ư","ừ","ứ","ự","ử","ữ"};
  for(int i=0; i<11; i++) s.replace(u_chars[i], "u");

  const char* y_chars[] = {"ỳ","ý","ỵ","ỷ","ỹ"};
  for(int i=0; i<5; i++) s.replace(y_chars[i], "y");

  s.replace("Đ", "d");
  s.replace("đ", "d");

  return s;
}

// ============================================================
//  Trích xuất số thực từ chuỗi, tìm về bên trái của vị trí pos
//  Ví dụ: text="turn left in 200 m", pos=17 -> 200.0
//         text="in 1.2 km", pos=7 -> 1.2
// ============================================================
float NavigationParser::extractNumber(const String& text, int endPos) {
  if (endPos <= 0) return 0;
  
  // Tìm đầu của số (đi ngược từ endPos)
  int start = endPos - 1;
  while (start > 0 && (isDigit(text.charAt(start - 1)) || text.charAt(start - 1) == '.')) {
    start--;
  }
  
  if (start >= endPos) return 0;
  
  String numStr = text.substring(start, endPos);
  numStr.trim();
  
  return numStr.toFloat();
}
