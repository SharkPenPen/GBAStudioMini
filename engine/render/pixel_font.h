// engine/render/pixel_font.h —— 用 8x8 位图字体在 DrawList 上画文字
//
// 不依赖纹理/系统字体：每个字形按行做游程合并，画成少量矩形。
// 适合运行时 HUD / 提示文字；中文需要更大的字体表，后续再补。
#pragma once

#include <cstdint>
#include <string>

#include "engine/render/draw_list.h"
#include "engine/render/font8x8.h"
#include "engine/render/font_cjk12.h"

namespace eng {

// UTF-8 解码：返回码点，*adv 为消耗的字节数
inline uint32_t Utf8Next(const std::string& s, size_t i, int* adv) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) { *adv = 1; return c; }
  if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
    *adv = 2;
    return ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
  }
  if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
    *adv = 3;
    return ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
  }
  *adv = 1;
  return '?';
}

// 查 12x12 中文字形；找不到返回 nullptr
inline const CjkGlyph* FindCjk(uint32_t code) {
  int lo = 0, hi = kCjkCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (kCjk12[mid].code == code) return &kCjk12[mid];
    if (kCjk12[mid].code < code) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

// 在 (x, y) 画一行文字（ASCII + 中文），scale 为整数倍放大
inline void DrawPixelText(DrawList& dl, float x, float y, const std::string& text,
                          uint32_t color, int scale = 1) {
  if (scale < 1) scale = 1;
  float cx = x;
  for (size_t i = 0; i < text.size();) {
    int adv = 1;
    const uint32_t code = Utf8Next(text, i, &adv);
    i += static_cast<size_t>(adv);
    if (code == '\n') {
      cx = x;
      y += static_cast<float>(kFontH * scale + scale);
      continue;
    }
    if (code >= 0x80) {
      const CjkGlyph* g = FindCjk(code);
      if (g == nullptr) {
        cx += static_cast<float>((kCjkW + 1) * scale);
        continue;
      }
      for (int ry = 0; ry < kCjkH; ++ry) {
        const uint16_t bits = g->rows[ry];
        int rx = 0;
        while (rx < kCjkW) {
          if ((bits & (1u << rx)) == 0) { ++rx; continue; }
          int run = 1;
          while (rx + run < kCjkW && (bits & (1u << (rx + run))) != 0) ++run;
          dl.DrawRect(Rect2{Vec2{cx + static_cast<float>(rx * scale),
                                 y + static_cast<float>(ry * scale)},
                            Vec2{static_cast<float>(run * scale),
                                 static_cast<float>(scale)}},
                      color);
          rx += run;
        }
      }
      cx += static_cast<float>((kCjkW + 1) * scale);
      continue;
    }
    unsigned char ch = static_cast<unsigned char>(code);
    if (ch < kFontFirst || ch > kFontLast) ch = '?';
    const uint8_t* rows = kFont8x8[ch - kFontFirst];
    for (int ry = 0; ry < kFontH; ++ry) {
      const uint8_t bits = rows[ry];
      int rx = 0;
      while (rx < kFontW) {
        if ((bits & (1u << rx)) == 0) {
          ++rx;
          continue;
        }
        int run = 1;
        while (rx + run < kFontW && (bits & (1u << (rx + run))) != 0) ++run;
        dl.DrawRect(Rect2{Vec2{cx + static_cast<float>(rx * scale),
                               y + static_cast<float>(ry * scale)},
                          Vec2{static_cast<float>(run * scale),
                               static_cast<float>(scale)}},
                    color);
        rx += run;
      }
    }
    cx += static_cast<float>((kFontW + 1) * scale);
  }
}

// 粗略宽度（用于居中）
inline float PixelTextWidth(const std::string& text, int scale = 1) {
  return static_cast<float>(text.size() * (kFontW + 1) * scale);
}

}  // namespace eng
