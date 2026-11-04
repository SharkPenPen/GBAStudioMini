// engine/base/color.h
// 颜色统一 RGBA8：ToU32() = r | g<<8 | b<<16 | a<<24（R 在最低字节）。
// 软光栅、纹理、窗口都按这个顺序，不要写成 0xRRGGBBAA。
#pragma once

#include "base/math.h"
#include "base/types.h"

namespace eng {

struct Color {
  u8 r = 255;
  u8 g = 255;
  u8 b = 255;
  u8 a = 255;

  // R 在最低字节；与软光栅/纹理/窗口的 RGBA8 顺序一致
  constexpr u32 ToU32() const {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) |
           (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
  }

  // 从 0x??BBGGRR 形式读回（即 ToU32 的逆）
  static constexpr Color FromU32(u32 v) {
    return Color{static_cast<u8>(v & 0xFFu), static_cast<u8>((v >> 8) & 0xFFu),
                 static_cast<u8>((v >> 16) & 0xFFu), static_cast<u8>((v >> 24) & 0xFFu)};
  }

  static constexpr Color RGB(u8 rr, u8 gg, u8 bb) { return Color{rr, gg, bb, 255}; }
  static constexpr Color RGBA(u8 rr, u8 gg, u8 bb, u8 aa) { return Color{rr, gg, bb, aa}; }

  // clamp 后 +0.5 再截断
  static constexpr u8 FromF32Component(f32 x) {
    const f32 c = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return static_cast<u8>(255.0f * c + 0.5f);
  }
  static constexpr Color FromF32(f32 rr, f32 gg, f32 bb, f32 aa = 1.0f) {
    return Color{FromF32Component(rr), FromF32Component(gg), FromF32Component(bb),
                 FromF32Component(aa)};
  }

  constexpr bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

// ★ 常量在 .rodata 里常量初始化，无运行期构造 ★（D-03a 允许这种）
constexpr Color kColorWhite{255, 255, 255, 255};
constexpr Color kColorBlack{0, 0, 0, 255};
constexpr Color kColorRed{255, 0, 0, 255};
constexpr Color kColorGreen{0, 255, 0, 255};
constexpr Color kColorBlue{0, 0, 255, 255};
constexpr Color kColorGray{128, 128, 128, 255};
constexpr Color kColorTransparent{0, 0, 0, 0};

// ★ 交换 R 与 B 两个字节 ★
//
// 为什么放在 base（而不是各平台自己写一份）：
//   这是 ★ 颜色格式的事，不是某个平台的事 ★ —— 而它正是"红蓝互换"那类
//   bug 的唯一拦截点，所以必须在 ★ 两端都跑到 ★ 的 base 自检里有断言。
//
// Windows 32 位 DIB 的内存序是 B,G,R,A，所以呈现前要换一次 R/B。
// 示例（1,2,3,4）：
//   引擎 ToU32()       = 0x04030201
//   SwapRB(0x04030201) = 0x04010203  ← DIB 里 B=3 在最低字节
constexpr u32 SwapRB(u32 v) {
  return (v & 0xFF00FF00u) | ((v & 0x00FF0000u) >> 16) | ((v & 0x000000FFu) << 16);
}

// 逐分量线性插值（含 alpha）
constexpr Color Lerp(const Color& a, const Color& b, f32 t) {
  return Color{static_cast<u8>(static_cast<f32>(a.r) + (static_cast<f32>(b.r) - a.r) * t),
               static_cast<u8>(static_cast<f32>(a.g) + (static_cast<f32>(b.g) - a.g) * t),
               static_cast<u8>(static_cast<f32>(a.b) + (static_cast<f32>(b.b) - a.b) * t),
               static_cast<u8>(static_cast<f32>(a.a) + (static_cast<f32>(b.a) - a.a) * t)};
}

}  // namespace eng
