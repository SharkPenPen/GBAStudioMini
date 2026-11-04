// engine/render/png.h
// PNG 解码（含 inflate）。两层：InflateRaw/ZlibInflate 与 PngDecode。
// 调用方提供 rgba/scratch 缓冲，解码器不做运行期分配。
#pragma once

#include "base/types.h"

namespace eng {

// ===========================================================================
// ★ 解压（RFC1951 / RFC1950）★
//
// 返回写出的字节数；失败返回 -1 并记日志（★ 不返回 0 冒充成功 ★）。
//   ★ 解压出 0 字节是合法的（空输入），所以失败必须用 -1 区分 ★。

// 裸 deflate 流
int InflateRaw(const u8* src, int src_size, u8* dst, int dst_cap);

// zlib 流：2 字节头 + deflate + 4 字节 Adler-32
//   ★ 校验和不匹配 ⇒ 返回 -1 ★（静默接受坏数据比报错更糟）
int ZlibInflate(const u8* src, int src_size, u8* dst, int dst_cap);

// Adler-32（给自检用，也给 zlib 流校验用）
u32 Adler32(const u8* data, int size);

// ===========================================================================
// ★ PNG ★
struct PngImage {
  u8* rgba = nullptr;  // ★ 一律扩成 RGBA8 ★（调用方的路径只有一条）
  int width = 0;
  int height = 0;
  // 诊断：源文件是什么格式（★ 便于"为什么颜色不对"这类问题归因 ★）
  int src_channels = 0;    // 1=灰 2=灰+α 3=RGB 4=RGBA
  int src_bit_depth = 0;
  bool src_palette = false;
};

// 解码。★ 失败返回 false 并把原因写进 why（★ 不静默 ★）★
//
// ★★ 为什么连 scratch 缓冲都要调用方给 ★★
//   滤镜解码需要一块 "height × (1 + width*channels)" 的临时区。
//   ★ 引擎侧不留静态、不分配 ★ —— 否则每个平台都要为一个
//   "可能永远不调用"的功能付常驻内存。
//   ⇒ 需要多少由调用方按自己的上限给，引擎只检查够不够。
bool PngDecode(const u8* data, int size, u8* rgba_buf, int rgba_cap, u8* scratch,
               int scratch_cap, PngImage* out, char* why, int why_cap);

// 解 PNG 需要多大的 scratch（width/height 由 PNG 头里读；这里只给上限公式）
//
// ★ 调用方按自己的最大图尺寸算一次即可 ★
// ★★ 注意：PngDecode 把 scratch 分成两半 ★★
//   后半段装 IDAT（压缩数据），前半段装解压后的扫描行。
//   ⇒ 只按 raw 大小给是不够的：1920x1041 的 PNG 会需要两份。
//   R1a 导入真实截图时踩到过这个边界（scratch 只给了 raw 的一半）。
constexpr int PngScratchBytes(int max_w, int max_h, int max_channels) {
  const int raw = (max_h + 1) * (1 + max_w * max_channels);
  return raw * 2 + 64000;  // IDAT + raw + 余量
}

}  // namespace eng
