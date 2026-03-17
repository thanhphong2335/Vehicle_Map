#include <Arduino.h>
#include <U8g2lib.h>
#include <TJpg_Decoder.h>
#include <Wire.h>

// --- 1. KHAI BÁO MÀN HÌNH ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- 2. ĐỊNH NGHĨA STRUCT (Bắt buộc nằm trên cùng) ---
struct VideoInfo {
    const uint8_t* const* frames;
    const uint16_t* frame_sizes;
    uint16_t num_frames;
};

// --- 3. ĐỊNH NGHĨA HÀM CALLBACK VẼ JPG ---
bool tjpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= u8g2.getDisplayHeight()) return false;
  for (uint16_t j = 0; j < h; j++) {
    for (uint16_t i = 0; i < w; i++) {
      uint16_t color = bitmap[i + j * w];
      if (color > 0x7BEF) u8g2.drawPixel(x + i, y + j);
    }
  }
  return true;
}

// --- 4. INCLUDE DỮ LIỆU (Sau khi đã có struct VideoInfo) ---
#include "video.h"   // Chứa video01
#include "image.h"   // Chứa image01
#include "video02.h" // Chứa video02
#include "video03.h" // Chứa video03
#include "video04.h"
#include "video05.h"
#include "video06.h"
#include "video07.h"
#include "video08.h"
// --- 5. HÀM CHẠY CHUNG ---
void playMedia(VideoInfo &data, int repeat, int pauseAfter) {
  for (int r = 0; r < repeat; r++) {
    for (int i = 0; i < data.num_frames; i++) {
      u8g2.clearBuffer();
      uint8_t* ptr = (uint8_t*)pgm_read_ptr(&(data.frames[i]));
      uint16_t sz = pgm_read_word(&(data.frame_sizes[i]));
      TJpgDec.drawJpg(0, 0, ptr, sz);
      u8g2.sendBuffer();
      if (data.num_frames > 1) delay(50);
    }
  }
  if (pauseAfter > 0) delay(pauseAfter);
}

void setup() {
  Wire.begin();
  Wire.setClock(400000);
  u8g2.begin();
  TJpgDec.setCallback(tjpg_output);
  TJpgDec.setJpgScale(1);
}

void loop() {
  playMedia(image01, 1, 3000);
  playMedia(video07, 5, 0);
  playMedia(video08, 5, 0);
  playMedia(video01, 1, 0);
  playMedia(video02, 1, 0); 
  playMedia(video03, 1, 0);
  playMedia(video04, 1, 0);
  playMedia(video05, 1, 0);
  playMedia(video06, 1, 0);
}