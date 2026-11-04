// engine/render/draw_list.h
// 一帧绘制命令缓冲：软件光栅、编辑器视口、命令流 diff 共用。
// 单屏 240x160；命令按录制顺序执行。
#pragma once

#include "base/array.h"
#include "base/color.h"
#include "base/mat.h"
#include "base/rect.h"
#include "base/types.h"
#include "render/texture.h"

namespace eng {

// ===========================================================================
enum class DrawCmdKind : u8 {
  kClear = 0,  // 把整个目标填成 color（等价于画一个铺满的矩形）
  kRect,       // 实心矩形
  kImage,      // 纹理（★ T1.5b 才实现，这里先占位 ★）
};

const char* DrawCmdKindName(DrawCmdKind k);

// ===========================================================================
// 一条绘制命令
//
// ★ 里面全是「已经算好的最终值」★：屏幕坐标、生效裁剪、最终颜色。
//   后端拿到它不需要再算任何东西 —— 这是两端一致的前提。
// ===========================================================================
// ★ 翻转标志（可以位或）★
//
//   ★ 翻转只改采样方向，不涉及任何取整 ★ ⇒ 对 L3 零风险。
//   PGMMV 也是这么做的（帧上一个"水平翻转"勾选项，不复制美术资源，
//   见 docs/notes 里对它官方教程的调研）。
constexpr u8 kDrawFlipH = 1u << 0;
constexpr u8 kDrawFlipV = 1u << 1;

struct DrawCmd {
  DrawCmdKind kind = DrawCmdKind::kClear;
  u8 flags = 0;  // kDrawFlipH / kDrawFlipV（只对 kImage 有意义）
  u8 pad1 = 0;

  Rect2 rect;  // ★ 目标矩形，屏幕坐标；kClear 时是整个目标 ★
  Rect2 clip;  // ★ 该命令生效的裁剪（已与栈求交）★
  u32 color = 0xFFFFFFFFu;  // kClear/kRect 的填充色；kImage 的 tint
  u32 texture = 0;          // kImage 的纹理 id

  // ★ kImage 的源区域（纹理像素，整数）★；全 0 = 整张
  TexRegion src;

  // 便于 dump 与自检的相等判断
  bool operator==(const DrawCmd& o) const {
    return kind == o.kind && flags == o.flags && rect == o.rect &&
           clip == o.clip && color == o.color && texture == o.texture && src == o.src;
  }
};

// ===========================================================================
class DrawList {
 public:
  // 一帧的命令上限：一屏地图 30x20=600 个 quad，加背景/UI/精灵留足余量。
  // 满了不崩、不静默丢：记账 + 打日志。
  static constexpr int kMaxCmds = 6144;
  static constexpr int kMaxTransformDepth = 16;
  static constexpr int kMaxClipDepth = 16;

  // ★ 每帧开始时调用 ★
  //
  // ★ 注意 Reset() 故意不清「屏幕尺寸」★ ——
  //   尺寸是★配置★（初始化时设一次），不是每帧状态；
  //   每帧都清的话调用方就得每帧重设一遍，那是白白引入的一类 bug。
  //   要回到"完全未配置"状态（自检、重新配置场景）用 ResetScreenSizes()。
  void Reset();

  // ★ 把屏幕尺寸也清掉 ★ —— 回到"完全未配置"状态。
  //   清掉之后 DrawRect 会拒绝记录并给出 WhyEmpty() 的原因，
  //   ★ 而不是用 0 尺寸的裁剪算出一个"看不见但也不报错"的结果 ★。
  void ResetScreenSizes();

  // ---- 目标尺寸 ----
  void SetScreenSize(int w, int h);
  int  ScreenWidth() const { return w_; }
  int  ScreenHeight() const { return h_; }
  Rect2 FullRect() const;

  // ---- 变换栈（2D 仿射）----
  void PushTransform(const Mat3& m);
  void PopTransform();
  int  TransformDepth() const { return xform_depth_; }
  Mat3 CurrentTransform() const;  // ★ 按值返回，避免任何静态对象（D-03a）★

  // ---- 裁剪栈（屏幕坐标）----
  void  PushClip(const Rect2& r);
  void  PopClip();
  int   ClipDepth() const { return clip_depth_; }
  Rect2 CurrentClip() const;

  // ---- 绘制 ----
  void Clear(u32 rgba);
  void DrawRect(const Rect2& r, u32 rgba);

  // ★ 把纹理（的一块）画进 dst 矩形 ★
  //
  // tint 的语义（★ R 在最低字节，A 在最高 ★）：
  //   - 全白 0xFFFFFFFF ⇒ 原样画（不混合、不染色）
  //   - RGB 全白、A < 255 ⇒ ★ 只做半透明 ★
  //   - RGB 非白 ⇒ ★ 染色（纹理 × tint，逐通道）★，A 同样参与不透明度
  //
  // src：★ 源区域（纹理像素，整数）★，默认全 0 = 整张纹理（M1 的行为）
  //      图集/动画帧就用它：同一张图上的不同格子在底层的表达。
  //      ★ 整数区域 ⇒ UV 精确 ⇒ 对 L3 零风险 ★
  // flags：kDrawFlipH / kDrawFlipV
  //      ★ 只改采样方向，不涉及取整 ⇒ 对 L3 零风险 ★
  //
  // 约定：最近邻采样（L3 要求，见 backend_soft.cpp）。
  void DrawImage(const Rect2& dst, TextureId tex, u32 tint = 0xFFFFFFFFu,
                 TexRegion src = TexRegion{}, u8 flags = 0);

  // ---- 读取 ----
  int  count() const { return static_cast<int>(cmds_.size()); }
  const DrawCmd& cmd(int i) const { return cmds_[i]; }
  const DrawCmd* cmds() const { return cmds_.data(); }

  bool overflowed() const { return overflowed_; }
  int  dropped() const { return dropped_; }

  // 一条命令都没记下的原因（给日志用）：返回 nullptr 表示不是"尺寸没设"
  const char* WhyEmpty() const;

 private:  void Push(DrawCmd c);
  // ★ 把屏幕坐标的矩形按当前变换烘焙成屏幕坐标（M1 取 4 角点的 AABB）★
  Rect2 Bake(const Rect2& r) const;

  Array<DrawCmd, kMaxCmds> cmds_;

  Mat3  xform_stack_[kMaxTransformDepth] = {};
  int   xform_depth_ = 0;

  Rect2 clip_stack_[kMaxClipDepth] = {};
  int   clip_depth_ = 0;

  int w_ = 0;
  int h_ = 0;

  bool overflowed_ = false;
  int  dropped_ = 0;

  // ★ 告警只打一次 ★：下面这些都是"每帧都会发生"的情况，
  //   每帧打一遍会把环形日志（64 条）冲爆，反而看不见别的问题。
  //   ★ mutable 是刻意的 ★：它们是"日志记账"而不是 DrawList 的逻辑状态，
  //   Bake()/录制路径是 const 的，不该为了打个日志把接口变成非 const。
  mutable bool warned_rotation_ = false;
  mutable bool warned_overflow_ = false;
  mutable bool warned_nosize_ = false;
  mutable bool warned_badtex_ = false;
  mutable bool warned_badregion_ = false;
};

// ===========================================================================
// ★ 命令流文本导出（T1.6）★
//
// 为什么需要：v1 只能看到「差 91696 个像素」，定位不到是哪一条绘制错了。
// 有了命令流 diff，就能指到具体哪一条。
//
// 逐行回调，不要调用方准备大缓冲：这里逐行喂出去，由调用方决定落盘还是转发。
//
// ★★ 坐标用定点整数打印（×256）★★
//   不用 `%f`：浮点格式化在不同 libc / 架构上可能给出不同文本
//   （尾数位数、指数写法、舍入），而 T1.6 的判据是「两端逐行一致」——
//   用 %f 会让那个判据变成「比两边的 libc 实现」，而不是「比引擎的语义」。
//   ×256 的精度对 2D 足够（1/256 像素），而且完全确定。
//
// 行格式（含结尾换行）：
//   第一行 : "cmds=%d overflow=%d dropped=%d"
//   之后每行: "[i] <Kind> rect=x,y,w,h clip=x,y,w,h color=RRGGBBAA tex=N"
//             （坐标是 ×256 的定点整数）
// ===========================================================================
using DrawDumpSink = void (*)(void* user, const char* line);
void DrawListDump(const DrawList& dl, DrawDumpSink sink, void* user);

// ===========================================================================
// 后端接口
//
// ★ 每个平台恰好一个后端 ★，与平台接口同样用函数指针表：
//   D-03a 禁全局构造，`const IRenderBackend kRenderBackend = {...}` 是常量初始化。
//
// ★ 一帧的顺序 ★
//     platform.begin_frame();      // 清输入边沿等
//     dl.Reset(); …录制…
//     backend.submit(dl);          // ★ 把命令变成像素 ★
//     platform.end_frame();        // 呈现：blit 到窗口
// ===========================================================================
struct IRenderBackend {
  // 初始化（分配后端自己的资源）。失败返回 false。
  bool (*init)();
  void (*shutdown)();

  // ★ 把 DrawList 变成像素 ★
  void (*submit)(const DrawList& dl);
  // 释放后端缓存的单张纹理（引擎 TextureDestroy 之后调用）
  void (*release_texture)(TextureId id);

  const char* name;
};

// 由 render/<target>/ 提供 —— 编译期只有一份被编进来
extern const IRenderBackend kRenderBackend;
inline const IRenderBackend& RenderBackend() { return kRenderBackend; }

}  // namespace eng
