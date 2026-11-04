// engine/render/font.h
//
// ★ 位图字体（运行期）★（docs/03 的 T5.6）
//
// 数据来自 `game_font.h`（由 tools/make-font-atlas.py 自动生成，1 位位打包）。
// 这里负责★把它展开成 RGBA8 纹理 + 字形表★，并回答"这个字怎么画"。
//
// ★★ 为什么运行期要展开成 RGBA8，而不是直接用 1 位 ★★
//   DrawList 只有 RGBA8 纹理一条路（M1 定的）。★ 再加一种纹理格式意味着
//   光栅化、GPU 上传、翻转缓存…每个环节都要支持两种格式 ★ ——
//   而字体图集很小（160x64），展开一次只占 40KB。
//   ▶ 将来要做 ETC1 时，同理：★ 格式差异只在一处转换 ★ 才是对的。
//
// ★★ 缺字形怎么办 ★★
//   ★ 用 .notdef 代替（画一个空心框）并★计数★ ★ —— 不静默画空白。
//   "文字少了一个字"和"文字整段不见"是两种完全不同的故障，必须能区分。
#pragma once

#include "base/types.h"
#include "render/texture.h"

namespace eng {

// 字形视图（指向内部表，不拥有）
struct GlyphView {
  bool valid = false;
  i32 x = 0;      // 图集里的矩形
  i32 y = 0;
  i32 w = 0;
  i32 h = 0;
  i32 dx = 0;     // 相对笔位（行顶）的偏移
  i32 dy = 0;
  i32 advance = 0;
};

// ★ 初始化：展开位图 → 建纹理 → 填字形表 ★
//   返回 false = 失败（纹理表满等），原因会记日志。
//   ★ 幂等 ★：重复调用只做一次。
bool FontInit();

// 字体图集纹理（未初始化返回 kInvalidTexture）
TextureId FontTexture();

// 行高（ascent + descent）
int FontLineHeight();
int FontAscent();

// ★ 查字形 ★
//   ★ 找不到返回 .notdef 并让 out->valid = false ★ —— 调用方据此计数。
//   ★ 所以"能画"和"画的是替身"是两件可区分的事 ★。
void FontGlyph(u32 codepoint, GlyphView* out);

// ★ 量一行文字的宽度 ★（不含换行；用于居中/右对齐）
//   ★ 缺字形按 .notdef 的 advance 算 ★ —— 量出来的宽度和画出来的必须一致。
int FontMeasure(const char* utf8, int char_spacing);

// ★ UTF-8 解码一个码点 ★
//   ★ 返回消耗的字节数（0 = 字符串结束，-1 = 非法序列）★。
//   非法序列不静默跳过：返回 -1，调用方画 .notdef 并计数。
int Utf8Decode(const char* s, u32* out_cp);

// 诊断：缺字形的次数（每次 FontGlyph 找不到就 +1）
int FontMissingGlyphCount();
void FontResetMissingGlyphCount();

}  // namespace eng
