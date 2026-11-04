// engine/render/texture.cpp
//
// ETC1 路径已按 docs/module-audit.md 删除：第一版只用 RGBA8。
#include "render/texture.h"

#include "base/log.h"

namespace eng {

namespace {

// ★ 零/常量初始化 → 没有运行期构造器 ★
Texture g_textures[kMaxTextures] = {};
int g_live = 0;
int g_peak = 0;

}  // namespace

// ===========================================================================
TextureId TextureCreate(const u8* rgba8, int width, int height) {
  if (rgba8 == nullptr || width <= 0 || height <= 0) {
    log_printf(LogLevel::kError, "texture: 参数非法 (%dx%d)", width, height);
    return kInvalidTexture;
  }
  for (int i = 0; i < kMaxTextures; ++i) {
    if (g_textures[i].used) {
      continue;
    }
    g_textures[i].rgba8 = rgba8;
    g_textures[i].width = width;
    g_textures[i].height = height;
    g_textures[i].used = true;
    ++g_live;
    if (g_live > g_peak) {
      g_peak = g_live;
    }
    return static_cast<TextureId>(i + 1);
  }
  // ★ 表满：明确失败，不覆盖已有的 ★
  log_printf(LogLevel::kError, "texture: 表满（上限 %d），创建失败", kMaxTextures);
  return kInvalidTexture;
}

const Texture* TextureGet(TextureId id) {
  if (id == kInvalidTexture || id > static_cast<TextureId>(kMaxTextures)) {
    return nullptr;
  }
  const Texture& t = g_textures[id - 1];
  return t.used ? &t : nullptr;
}

void TextureDestroy(TextureId id) {
  if (id == kInvalidTexture || id > kMaxTextures) return;
  Texture& t = g_textures[id - 1];
  if (!t.used) return;
  t.used = false;
  t.rgba8 = nullptr;
  t.width = 0;
  t.height = 0;
  if (g_live > 0) --g_live;
}

void TextureResetAll() {
  for (int i = 0; i < kMaxTextures; ++i) {
    g_textures[i].used = false;
    g_textures[i].rgba8 = nullptr;
    g_textures[i].width = 0;
    g_textures[i].height = 0;
  }
  g_live = 0;
  // ★ peak 不重置：它是"历史最高"★
}

int TextureLiveCount() { return g_live; }

int TexturePeakCount() { return g_peak; }

}  // namespace eng
