// PC 软件光栅：把 DrawList 画进平台软帧缓冲，CPU 结果确定，供编辑器视口/验收。
#include "platform/platform.h"
#include "render/backend_soft.h"
#include "render/draw_list.h"

#include "base/log.h"
#include "base/math.h"

namespace eng {

namespace {

SoftFrameBuffer g_fb;
bool g_warned_badtex = false;

// 半透明混合：src-over，8 位整数，四舍五入。
// out = (src*a + dst*(255-a) + 127) / 255
// 结果 alpha 固定为 255；软帧缓冲不保存目标 alpha。
inline u32 BlendSrcOver(u32 src, u32 dst) {
  const u32 sa = (src >> 24) & 0xFFu;
  if (sa == 255u) {
    return src;
  }
  if (sa == 0u) {
    return dst;
  }
  const u32 ia = 255u - sa;
  const u32 sr = src & 0xFFu, sg = (src >> 8) & 0xFFu, sb = (src >> 16) & 0xFFu;
  const u32 dr = dst & 0xFFu, dg = (dst >> 8) & 0xFFu, db = (dst >> 16) & 0xFFu;
  const u32 r = (sr * sa + dr * ia + 127u) / 255u;
  const u32 g = (sg * sa + dg * ia + 127u) / 255u;
  const u32 b = (sb * sa + db * ia + 127u) / 255u;
  return r | (g << 8) | (b << 16) | 0xFF000000u;
}

// ★ 染色：逐通道 out = tex × tint / 255，四舍五入 ★
//   逐通道 out = tex * tint / 255。
inline u32 ApplyTint(u32 texel, u32 tint) {
  if (tint == 0xFFFFFFFFu) {
    return texel;  // 快路径：不染色（M1 的用例全走这里，行为与之前完全一致）
  }
  const u32 r = ((texel & 0xFFu) * (tint & 0xFFu) + 127u) / 255u;
  const u32 g = (((texel >> 8) & 0xFFu) * ((tint >> 8) & 0xFFu) + 127u) / 255u;
  const u32 b = (((texel >> 16) & 0xFFu) * ((tint >> 16) & 0xFFu) + 127u) / 255u;
  const u32 a = (((texel >> 24) & 0xFFu) * ((tint >> 24) & 0xFFu) + 127u) / 255u;
  return r | (g << 8) | (b << 16) | (a << 24);
}

// ★ 左闭右开 ★，并与目标边界求交
void FillRect(SoftFrameBuffer fb, Rect2 r, u32 rgba) {
  if (!fb.valid() || r.size.x <= 0.0f || r.size.y <= 0.0f) {
    return;
  }
  int x0 = static_cast<int>(Floor(r.position.x));
  int y0 = static_cast<int>(Floor(r.position.y));
  int x1 = static_cast<int>(Floor(r.position.x + r.size.x));
  int y1 = static_cast<int>(Floor(r.position.y + r.size.y));

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > fb.width) x1 = fb.width;
  if (y1 > fb.height) y1 = fb.height;

  // ★ 不透明色走原来那条路（纯覆盖）★ ——
  //   于是 M1 那些"零差异"用例的行为一点没变，不受这次改动影响。
  const bool opaque = ((rgba >> 24) & 0xFFu) == 255u;
  for (int y = y0; y < y1; ++y) {
    u32* row = fb.pixels + static_cast<usize>(y) * static_cast<usize>(fb.pitch);
    for (int x = x0; x < x1; ++x) {
      row[x] = opaque ? rgba : BlendSrcOver(rgba, row[x]);
    }
  }
}

// ★ 最近邻缩放画纹理 ★
//
// ★ 为什么是最近邻，不是双线性 ★
//   L3（逐像素同帧）要求两端结果一致。双线性在 GPU 上是另一套权重/取整规则，
//   几乎不可能与 CPU 实现逐位对齐；最近邻是唯一容易做到「两端完全一致」的采样。
//   ★ 而且 dst 尺寸 == 纹理尺寸时（M1 的首选用例）任何滤波都退化成同一个结果 ★。
//
// 采样规则：取像素★中心★（+0.5）对应的纹素 —— 最近邻的标准做法，
// 也让 1:1 的整数对齐拷贝恰好是恒等映射。
// ★ 落在 dst 之外的像素直接跳过，不做边缘钳位 ★ ——
// 钳位会在"裁剪框比目标矩形大"时把边缘纹素拖成一条色带，那是看不出原因的错。
//
// ★ 源区域（图集）与翻转 ★
//   采样先在★区域内★算出局部坐标 lu/lv（0..src_w-1），再加上 src.x/src.y；
//   翻转只是把 lu 变成 src_w-1-lu —— ★ 纯整数镜像，不引入任何取整 ★。
//   所以图集与翻转都不会破坏"两端零差异"（这一点由 verify-all 里的场景守着）。
void BlitImage(SoftFrameBuffer fb, Rect2 dst_full, Rect2 dst_clipped, const Texture& tex,
               u32 tint, TexRegion src, u8 flags) {
  if (!fb.valid() || tex.width <= 0 || tex.height <= 0) {
    return;
  }
  if (dst_full.size.x <= 0.0f || dst_full.size.y <= 0.0f) {
    return;
  }
  const int sw = src.whole() ? tex.width : static_cast<int>(src.w);
  const int sh = src.whole() ? tex.height : static_cast<int>(src.h);
  const int sx = src.whole() ? 0 : static_cast<int>(src.x);
  const int sy = src.whole() ? 0 : static_cast<int>(src.y);
  if (sw <= 0 || sh <= 0) {
    return;
  }

  int x0 = static_cast<int>(Floor(dst_clipped.position.x));
  int y0 = static_cast<int>(Floor(dst_clipped.position.y));
  int x1 = static_cast<int>(Floor(dst_clipped.position.x + dst_clipped.size.x));
  int y1 = static_cast<int>(Floor(dst_clipped.position.y + dst_clipped.size.y));
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > fb.width) x1 = fb.width;
  if (y1 > fb.height) y1 = fb.height;

  const f32 nw = static_cast<f32>(sw);
  const f32 nh = static_cast<f32>(sh);
  const bool flip_h = (flags & kDrawFlipH) != 0;
  const bool flip_v = (flags & kDrawFlipV) != 0;

  for (int y = y0; y < y1; ++y) {
    const f32 v = (static_cast<f32>(y) + 0.5f - dst_full.position.y) / dst_full.size.y;
    if (v < 0.0f || v >= 1.0f) {
      continue;
    }
    int lv = static_cast<int>(v * nh);
    if (flip_v) {
      lv = sh - 1 - lv;
    }
    const int ty = sy + lv;
    u32* row = fb.pixels + static_cast<usize>(y) * static_cast<usize>(fb.pitch);
    for (int x = x0; x < x1; ++x) {
      const f32 u = (static_cast<f32>(x) + 0.5f - dst_full.position.x) / dst_full.size.x;
      if (u < 0.0f || u >= 1.0f) {
        continue;
      }
      int lu = static_cast<int>(u * nw);
      if (flip_h) {
        lu = sw - 1 - lu;
      }
      const int tx = sx + lu;
      // 采样 → 染色 → 混合（不透明时 BlendSrcOver 会走快路径，结果与原来相同）
      const u32 texel = ApplyTint(TexturePixelU32(tex, tx, ty), tint);
      row[x] = BlendSrcOver(texel, row[x]);
    }
  }
}

bool SoftInit() { return true; }

void SoftShutdown() {}

// ★ 画到★指定的★帧缓冲（无平台依赖）★
//   ★ 光栅化代码只有这一份 ★ —— 编辑器视口和游戏窗口的像素出自同一个函数。
void SoftSubmitToImpl(const DrawList& dl, SoftFrameBuffer target) {
  const char* why = dl.WhyEmpty();
  if (why != nullptr) {
    log_printf(LogLevel::kError, "soft 后端：%s", why);
    return;
  }

  g_fb = target;
  if (!g_fb.valid()) {
    log_printf(LogLevel::kError, "soft 后端：目标帧缓冲无效");
    return;
  }

  // ★ 按录制顺序执行 ★ —— 顺序就是语义，不做任何重排或合并
  for (int i = 0; i < dl.count(); ++i) {
    const DrawCmd& c = dl.cmd(i);
    const SoftFrameBuffer fb = g_fb;

    switch (c.kind) {
      case DrawCmdKind::kClear:
        // Clear = 铺满当前屏的矩形（见 draw_list.cpp 的说明）
        FillRect(fb, c.rect, c.color);
        break;

      case DrawCmdKind::kRect:
        FillRect(fb, c.rect.Intersection(c.clip), c.color);
        break;

      case DrawCmdKind::kImage: {
        const Texture* t = TextureGet(c.texture);
        if (t == nullptr || !t->valid()) {
          if (!g_warned_badtex) {
            g_warned_badtex = true;
            log_printf(LogLevel::kError, "soft 后端：Image 命令引用了无效纹理");
          }
          break;
        }
        BlitImage(fb, c.rect, c.rect.Intersection(c.clip), *t, c.color, c.src, c.flags);
        break;
      }
    }
  }
}

// ★ 从平台取目标再画 ★ —— 这是 SoftSubmitTo 的一层薄包装
void SoftSubmit(const DrawList& dl) {
  SoftSubmitToImpl(dl, Platform().soft_framebuffer());
}

}  // namespace

// ★ 公开入口（头文件里声明的那个）★
//   实现放在匿名命名空间里，这里只做转调 ——
//   否则匿名命名空间里的定义和头文件的声明会撞名造成重载歧义。
void SoftSubmitTo(const DrawList& dl, SoftFrameBuffer target) {
  SoftSubmitToImpl(dl, target);
}

void SoftReleaseTexture(TextureId id) { (void)id; }

const IRenderBackend kRenderBackend = {
    SoftInit,
    SoftShutdown,
    SoftSubmit,
    SoftReleaseTexture,
    "soft",
};

}  // namespace eng
