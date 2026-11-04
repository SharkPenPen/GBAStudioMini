// engine/render/texture.h
// 纹理：RGBA8 最小集合；不做 ETC1/压缩。像素顺序 R,G,B,A，不拥有数据。
#pragma once

#include "base/types.h"

namespace eng {

// 句柄：0 = 无效，>=1 是「下标 + 1」
using TextureId = u32;
constexpr TextureId kInvalidTexture = 0;

struct Texture {
  const u8* rgba8 = nullptr;
  int  width = 0;
  int  height = 0;
  bool used = false;

  bool valid() const {
    return used && rgba8 != nullptr && width > 0 && height > 0;
  }
};

// ===========================================================================
// ★ 纹理里的一块（图集的一个格子 / 一帧动画）★
//
// 单位是★纹理像素★，而且是★整数★：整数区域 ⇒ UV 精确 ⇒ 采样不引入取整分歧。
// 全 0 表示「整张纹理」。
// ===========================================================================
struct TexRegion {
  i32 x = 0;
  i32 y = 0;
  i32 w = 0;
  i32 h = 0;

  bool whole() const { return w <= 0 || h <= 0; }

  static TexRegion Whole() { return TexRegion{}; }
  static TexRegion Make(i32 xx, i32 yy, i32 ww, i32 hh) { return TexRegion{xx, yy, ww, hh}; }

  bool operator==(const TexRegion& o) const {
    return x == o.x && y == o.y && w == o.w && h == o.h;
  }
};

// 定长表（无动态分配）
constexpr int kMaxTextures = 32;

// 创建：返回 0 表示失败（表满 / 参数非法）。失败会记日志，不静默。
TextureId TextureCreate(const u8* rgba8, int width, int height);

const Texture* TextureGet(TextureId id);

// 释放单个纹理槽
void TextureDestroy(TextureId id);

// 清空（自检与场景重建用）
void TextureResetAll();

int TextureLiveCount();
int TexturePeakCount();

// 取纹理某像素的引擎 u32（R 在最低字节，见 base/color.h）。
inline u32 TexturePixelU32(const Texture& t, int x, int y) {
  const u8* p = t.rgba8 +
                (static_cast<usize>(y) * static_cast<usize>(t.width) +
                 static_cast<usize>(x)) * 4u;
  return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
         (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

}  // namespace eng
