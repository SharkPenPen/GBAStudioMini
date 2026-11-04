// 字体图集展开（RGBA8，静态存储）。
#include "render/font.h"

#include <cstring>

#include "base/log.h"
#include "render/game_font.h"

namespace eng {

namespace {

// ★ 展开后的 RGBA8 图集 ★
//   静态存储：生命周期与纹理一致。
//     （TextureCreate 只存指针，不拷贝）★。
u8 g_atlas_rgba[kGameFontAtlasW * kGameFontAtlasH * 4];

TextureId g_tex = kInvalidTexture;
bool g_tried = false;
int g_missing = 0;

// .notdef：一个空心框（★ 看得见，才知道"这里缺字"★）
GlyphView g_notdef;

inline bool BitAt(int x, int y) {
  if (x < 0 || x >= kGameFontAtlasW || y < 0 || y >= kGameFontAtlasH) {
    return false;
  }
  const int idx = y * kGameFontPitch + (x >> 3);
  const u8 b = kGameFontBits[idx];
  // ★ 每行 MSB 先出 ★（生成器和这里必须一致）
  return ((b >> (7 - (x & 7))) & 1u) != 0;
}

}  // namespace

// ===========================================================================
int Utf8Decode(const char* s, u32* out_cp) {
  if (s == nullptr || out_cp == nullptr) {
    return -1;
  }
  const u8* p = reinterpret_cast<const u8*>(s);
  if (p[0] == 0) {
    *out_cp = 0;
    return 0;
  }
  if (p[0] < 0x80) {
    *out_cp = p[0];
    return 1;
  }
  if ((p[0] & 0xE0) == 0xC0) {
    if ((p[1] & 0xC0) != 0x80) {
      return -1;
    }
    *out_cp = (static_cast<u32>(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    return 2;
  }
  if ((p[0] & 0xF0) == 0xE0) {
    if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) {
      return -1;
    }
    *out_cp = (static_cast<u32>(p[0] & 0x0F) << 12) |
              (static_cast<u32>(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return 3;
  }
  if ((p[0] & 0xF8) == 0xF0) {
    if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) {
      return -1;
    }
    *out_cp = (static_cast<u32>(p[0] & 0x07) << 18) |
              (static_cast<u32>(p[1] & 0x3F) << 12) |
              (static_cast<u32>(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return 4;
  }
  return -1;
}

// ===========================================================================
bool FontInit() {
  if (g_tried) {
    return g_tex != kInvalidTexture;
  }
  g_tried = true;

  // ★ 位图 → RGBA8 ★
  //   位=1 ⇒ 白色不透明；位=0 ⇒ 透明。
  //   ★ 颜色靠绘制时的 tint ★（一组字形可以画成任意颜色，不用改图集）。
  for (int y = 0; y < kGameFontAtlasH; ++y) {
    for (int x = 0; x < kGameFontAtlasW; ++x) {
      u8* t = &g_atlas_rgba[(y * kGameFontAtlasW + x) * 4];
      const bool on = BitAt(x, y);
      t[0] = 255;
      t[1] = 255;
      t[2] = 255;
      t[3] = on ? 255 : 0;
    }
  }

  g_tex = TextureCreate(g_atlas_rgba, kGameFontAtlasW, kGameFontAtlasH);
  if (g_tex == kInvalidTexture) {
    log_printf(LogLevel::kError, "font: 字体图集纹理创建失败（纹理表满？）");
    return false;
  }

  // .notdef：10x9 的空心框（画在"第一个空格子"的位置不合适，用图集里
  // 留空的右下角不行 —— 这里干脆★不占图集★，用程序生成：
  // ★ 因为它要保证"一定存在"，不能赌图集里有空位 ★。
  g_notdef.valid = false;
  g_notdef.x = 0;
  g_notdef.y = 0;
  g_notdef.w = 0;
  g_notdef.h = 0;
  g_notdef.dx = 0;
  g_notdef.dy = 0;
  g_notdef.advance = kGameFontCellW;
  return true;
}

TextureId FontTexture() {
  FontInit();
  return g_tex;
}

int FontLineHeight() { return kGameFontAscent + kGameFontDescent; }
int FontAscent() { return kGameFontAscent; }

int FontMissingGlyphCount() { return g_missing; }
void FontResetMissingGlyphCount() { g_missing = 0; }

// ===========================================================================
void FontGlyph(u32 codepoint, GlyphView* out) {
  if (out == nullptr) {
    return;
  }
  *out = GlyphView{};
  FontInit();
  for (int i = 0; i < kGameFontGlyphCount; ++i) {
    const GameFontGlyphRec& g = kGameFontGlyphs[i];
    if (g.cp == codepoint) {
      out->valid = true;
      out->x = g.x;
      out->y = g.y;
      out->w = g.w;
      out->h = g.h;
      out->dx = g.dx;
      out->dy = g.dy;
      out->advance = g.advance;
      return;
    }
  }
  // ★ 缺字形：给出 .notdef 的度量（能推进，不会把后面的字叠在一起），
  //   但 valid = false ⇒ 调用方可以计数 ★。
  ++g_missing;
  *out = g_notdef;
  out->valid = false;
}

// ===========================================================================
int FontMeasure(const char* utf8, int char_spacing) {
  if (utf8 == nullptr) {
    return 0;
  }
  int total = 0;
  const char* p = utf8;
  while (true) {
    u32 cp = 0;
    const int n = Utf8Decode(p, &cp);
    if (n <= 0) {
      break;
    }
    p += n;
    GlyphView g;
    FontGlyph(cp, &g);
    total += g.advance + char_spacing;
  }
  if (total > 0 && char_spacing != 0) {
    total -= char_spacing;  // 最后一个字后面不加间距
  }
  return total;
}

}  // namespace eng
