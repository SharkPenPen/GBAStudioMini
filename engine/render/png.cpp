// engine/render/png.cpp
//
// ★ RFC1951 (deflate) / RFC1950 (zlib) 解压 ★
//
// ★★ 算法为什么照 zlib 参考实现「puff」的写法 ★★
//   不是自己发明一套。puff 的做法是：
//     · 位读取器按 LSB 先出
//     · 规范 Huffman 只存 count[16] + symbol[]，
//       ★ 解码时按"首码 + 计数"逐位走 ★，不建查表
//   ★ 好处是★没有大表、栈上开销小★，而且★逻辑能和规范一节一节对上★ ——
//   自己优化出来的查表法一旦错一位，是"解出来的图看起来怪怪的"这种难查的错。
//   速度上，解码几张几十 KB 的图完全够（编辑器预览，不是每帧都跑）。
#include "render/png.h"

#include <cstring>

#include "base/log.h"

namespace eng {

namespace {

// ===========================================================================
// ★ 位读取器（LSB 先出）★
struct BitReader {
  const u8* p = nullptr;
  const u8* end = nullptr;
  u32 buf = 0;
  int cnt = 0;  // buf 里还有多少位
  bool bad = false;
};

inline void BrInit(BitReader* br, const u8* src, int size) {
  br->p = src;
  br->end = src + size;
  br->buf = 0;
  br->cnt = 0;
  br->bad = false;
}

// 确保至少有 n 位（n <= 24）
inline void BrNeed(BitReader* br, int n) {
  while (br->cnt < n) {
    if (br->p >= br->end) {
      br->bad = true;
      return;
    }
    br->buf |= static_cast<u32>(*br->p++) << br->cnt;
    br->cnt += 8;
  }
}

inline u32 BrBits(BitReader* br, int n) {
  if (n == 0) {
    return 0;
  }
  BrNeed(br, n);
  if (br->bad) {
    return 0;
  }
  const u32 v = br->buf & ((1u << n) - 1u);
  br->buf >>= n;
  br->cnt -= n;
  return v;
}

// 丢到下一个字节边界
inline void BrAlign(BitReader* br) {
  br->buf = 0;
  br->cnt = 0;
}

// ===========================================================================
// ★ 规范 Huffman 表 ★
//
// ★ 不用"码→符号"的查找表 ★，只存每一级码长的个数和符号序列。
//   解码时按 len=1..15 累积：如果当前码落在这一级的区间里，就命中。
struct HuffTable {
  u16 count[16];   // count[len] = 码长为 len 的符号个数
  u16 symbol[288]; // 按码长、再按符号值排序后的符号序列
};

// 由码长表构造（返回 0 表示码长表本身不合法）
int HuffBuild(HuffTable* h, const u8* lengths, int n) {
  std::memset(h, 0, sizeof(*h));
  for (int i = 0; i < n; ++i) {
    ++h->count[lengths[i]];
  }
  // ★ 完整性检查：不能有"全 0 的码长"占着位置（除 0 长度外）
  if (h->count[0] == n) {
    return 0;  // 全 0：合法的不完整码（用于空距离码）
  }
  int left = 1;
  for (int len = 1; len <= 15; ++len) {
    left <<= 1;
    left -= h->count[len];
    if (left < 0) {
      return -1;  // ★ 过订（over-subscribed）★
    }
  }
  // 偏移
  u16 offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; ++len) {
    offs[len + 1] = static_cast<u16>(offs[len] + h->count[len]);
  }
  for (int i = 0; i < n; ++i) {
    if (lengths[i] != 0) {
      h->symbol[offs[lengths[i]]++] = static_cast<u16>(i);
    }
  }
  return 1;
}

// 解一个符号；失败返回 -1
int HuffDecode(BitReader* br, const HuffTable* h) {
  int code = 0;
  int first = 0;
  int index = 0;
  for (int len = 1; len <= 15; ++len) {
    code |= static_cast<int>(BrBits(br, 1));
    if (br->bad) {
      return -1;
    }
    const int cnt = h->count[len];
    if (code - first < cnt) {
      return h->symbol[index + (code - first)];
    }
    index += cnt;
    first = (first + cnt) << 1;
    code <<= 1;
  }
  return -1;  // 超过 15 位还没命中 ⇒ 数据坏了
}

// ===========================================================================
// 长度/距离基表（RFC1951 §3.2.5）
const u16 kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                          15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                          67, 83, 99, 115, 131, 163, 195, 227, 258};
const u8 kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                          2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const u16 kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                           17,   25,   33,   49,   65,   97,    129,   193,
                           257,  385,  513,  769,  1025, 1537,  2049,  3073,
                           4097, 6145, 8193, 12289, 16385, 24577};
const u8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// 码长码的顺序（RFC1951 §3.2.7）
const u8 kCodeLenOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                              11, 4,  12, 3, 13, 2, 14, 1, 15};

// 静态工作区：这些表不放栈上。
HuffTable g_lencode;
HuffTable g_distcode;
u8 g_lengths[320];
u8 g_codelen[19];

// 一个块。dst 写到 out_pos 并推进。
bool InflateBlock(BitReader* br, const HuffTable* lencode, const HuffTable* distcode,
                  u8* dst, int dst_cap, int* out_pos) {
  for (;;) {
    const int sym = HuffDecode(br, lencode);
    if (sym < 0) {
      return false;
    }
    if (sym < 256) {
      if (*out_pos >= dst_cap) {
        log_printf(LogLevel::kError, "inflate: 输出缓冲区不够（> %d 字节）", dst_cap);
        return false;
      }
      dst[(*out_pos)++] = static_cast<u8>(sym);
      continue;
    }
    if (sym == 256) {
      return true;  // 块结束
    }
    // 长度
    const int li = sym - 257;
    if (li < 0 || li >= 29) {
      log_printf(LogLevel::kError, "inflate: 长度符号 %d 越界", sym);
      return false;
    }
    int len = kLenBase[li] + static_cast<int>(BrBits(br, kLenExtra[li]));
    if (br->bad) {
      return false;
    }
    const int dsym = HuffDecode(br, distcode);
    if (dsym < 0 || dsym >= 30) {
      log_printf(LogLevel::kError, "inflate: 距离符号 %d 非法", dsym);
      return false;
    }
    const int dist = kDistBase[dsym] + static_cast<int>(BrBits(br, kDistExtra[dsym]));
    if (br->bad || dist <= 0 || dist > *out_pos) {
      log_printf(LogLevel::kError, "inflate: 回引距离 %d 非法（已输出 %d）", dist,
                 *out_pos);
      return false;
    }
    if (*out_pos + len > dst_cap) {
      log_printf(LogLevel::kError, "inflate: 输出缓冲区不够（> %d 字节）", dst_cap);
      return false;
    }
    // ★ 逐字节复制（不能 memcpy：dist 可能小于 len，源和目标重叠）★
    for (int i = 0; i < len; ++i) {
      dst[*out_pos] = dst[*out_pos - dist];
      ++(*out_pos);
    }
  }
}

}  // namespace

// ===========================================================================
int InflateRaw(const u8* src, int src_size, u8* dst, int dst_cap) {
  if (src == nullptr || dst == nullptr || src_size <= 0 || dst_cap < 0) {
    log_printf(LogLevel::kError, "inflate: 参数非法");
    return -1;
  }
  static BitReader br;  // ★ 静态：别放栈上 ★
  BrInit(&br, src, src_size);

  int out_pos = 0;
  bool last = false;
  while (!last) {
    last = BrBits(&br, 1) != 0;
    const u32 type = BrBits(&br, 2);
    if (br.bad) {
      log_printf(LogLevel::kError, "inflate: 块头读不到");
      return -1;
    }
    if (type == 0) {
      // ★ 存储块（不压缩）★
      BrAlign(&br);
      if (br.p + 4 > br.end) {
        log_printf(LogLevel::kError, "inflate: 存储块头被截断");
        return -1;
      }
      const u32 len = static_cast<u32>(br.p[0]) | (static_cast<u32>(br.p[1]) << 8);
      const u32 nlen = static_cast<u32>(br.p[2]) | (static_cast<u32>(br.p[3]) << 8);
      br.p += 4;
      if ((len ^ 0xFFFFu) != nlen) {
        log_printf(LogLevel::kError, "inflate: 存储块 LEN/NLEN 不互补");
        return -1;
      }
      if (br.p + len > br.end || out_pos + static_cast<int>(len) > dst_cap) {
        log_printf(LogLevel::kError, "inflate: 存储块越界（len=%u）", len);
        return -1;
      }
      std::memcpy(dst + out_pos, br.p, len);
      br.p += len;
      out_pos += static_cast<int>(len);
      continue;
    }
    if (type == 1) {
      // ★ 固定 Huffman ★（码长表由规范写死）
      for (int i = 0; i < 144; ++i) g_lengths[i] = 8;
      for (int i = 144; i < 256; ++i) g_lengths[i] = 9;
      for (int i = 256; i < 280; ++i) g_lengths[i] = 7;
      for (int i = 280; i < 288; ++i) g_lengths[i] = 8;
      if (HuffBuild(&g_lencode, g_lengths, 288) < 0) {
        log_printf(LogLevel::kError, "inflate: 固定码表构造失败");
        return -1;
      }
      for (int i = 0; i < 30; ++i) g_lengths[i] = 5;
      if (HuffBuild(&g_distcode, g_lengths, 30) <= 0) {
        log_printf(LogLevel::kError, "inflate: 固定距离码表构造失败");
        return -1;
      }
      if (!InflateBlock(&br, &g_lencode, &g_distcode, dst, dst_cap, &out_pos)) {
        return -1;
      }
      continue;
    }
    if (type == 2) {
      // ★ 动态 Huffman ★
      const u32 hlit = BrBits(&br, 5) + 257;
      const u32 hdist = BrBits(&br, 5) + 1;
      const u32 hclen = BrBits(&br, 4) + 4;
      if (br.bad || hlit > 286 || hdist > 30) {
        log_printf(LogLevel::kError, "inflate: 动态码表头部非法 (hlit=%u hdist=%u)",
                   hlit, hdist);
        return -1;
      }
      std::memset(g_codelen, 0, sizeof(g_codelen));
      for (u32 i = 0; i < hclen; ++i) {
        g_codelen[kCodeLenOrder[i]] = static_cast<u8>(BrBits(&br, 3));
      }
      if (br.bad) {
        return -1;
      }
      // 码长码本身用一张临时表
      static HuffTable codelen_code;
      if (HuffBuild(&codelen_code, g_codelen, 19) <= 0) {
        log_printf(LogLevel::kError, "inflate: 码长码表不合法");
        return -1;
      }
      // 用码长码展开出 lit/dist 的码长表
      int n = 0;
      while (n < static_cast<int>(hlit + hdist)) {
        const int sym = HuffDecode(&br, &codelen_code);
        if (sym < 0) {
          log_printf(LogLevel::kError, "inflate: 码长码解码失败");
          return -1;
        }
        if (sym < 16) {
          g_lengths[n++] = static_cast<u8>(sym);
        } else {
          u8 repeat = 0;
          int times = 0;
          if (sym == 16) {
            if (n == 0) {
              log_printf(LogLevel::kError, "inflate: 16 号码长码出现在开头");
              return -1;
            }
            repeat = g_lengths[n - 1];
            times = 3 + static_cast<int>(BrBits(&br, 2));
          } else if (sym == 17) {
            times = 3 + static_cast<int>(BrBits(&br, 3));
          } else {
            times = 11 + static_cast<int>(BrBits(&br, 7));
          }
          if (br.bad || n + times > static_cast<int>(hlit + hdist)) {
            log_printf(LogLevel::kError, "inflate: 码长重复越界");
            return -1;
          }
          for (int i = 0; i < times; ++i) {
            g_lengths[n++] = repeat;
          }
        }
      }
      // lit/len 必须以 256（块结束）结尾
      if (g_lengths[256] == 0) {
        log_printf(LogLevel::kError, "inflate: 缺块结束符号（码长表[256] = 0）");
        return -1;
      }
      if (HuffBuild(&g_lencode, g_lengths, static_cast<int>(hlit)) <= 0) {
        log_printf(LogLevel::kError, "inflate: 字面量/长度码表不合法");
        return -1;
      }
      if (HuffBuild(&g_distcode, g_lengths + hlit, static_cast<int>(hdist)) < 0) {
        log_printf(LogLevel::kError, "inflate: 距离码表不合法");
        return -1;
      }
      if (!InflateBlock(&br, &g_lencode, &g_distcode, dst, dst_cap, &out_pos)) {
        return -1;
      }
      continue;
    }
    log_printf(LogLevel::kError, "inflate: 块类型 3 保留，非法");
    return -1;
  }
  return out_pos;
}

// ===========================================================================
u32 Adler32(const u8* data, int size) {
  u32 a = 1;
  u32 b = 0;
  if (data == nullptr || size <= 0) {
    return 1;  // 空串的 Adler-32
  }
  // 分段取模，避免溢出（NMAX = 5552 是 zlib 用的安全上限）
  int i = 0;
  while (i < size) {
    const int chunk = ((size - i) > 5552) ? 5552 : (size - i);
    for (int k = 0; k < chunk; ++k) {
      a += data[i + k];
      b += a;
    }
    a %= 65521u;
    b %= 65521u;
    i += chunk;
  }
  return (b << 16) | a;
}

int ZlibInflate(const u8* src, int src_size, u8* dst, int dst_cap) {
  if (src == nullptr || src_size < 6) {
    log_printf(LogLevel::kError, "zlib: 输入太短（%d）", src_size);
    return -1;
  }
  // ★ 头：CMF/FLG ★
  const u8 cmf = src[0];
  const u8 flg = src[1];
  if ((cmf & 0x0Fu) != 8) {
    log_printf(LogLevel::kError, "zlib: 压缩方法 %u 不支持（只支持 8 = deflate）",
               static_cast<unsigned>(cmf & 0x0Fu));
    return -1;
  }
  if (((static_cast<u32>(cmf) << 8) | flg) % 31u != 0) {
    log_printf(LogLevel::kError, "zlib: 头部校验失败（CMF/FLG 不是 31 的倍数）");
    return -1;
  }
  if ((flg & 0x20u) != 0) {
    // ★ 预置字典：本项目不用，明确拒绝 ★
    log_printf(LogLevel::kError, "zlib: 不支持预置字典（FDICT=1）");
    return -1;
  }
  const int n = InflateRaw(src + 2, src_size - 6, dst, dst_cap);
  if (n < 0) {
    return -1;
  }
  // ★ 校验和：★ 不匹配就失败，不静默接受坏数据 ★
  const u8* tail = src + src_size - 4;
  const u32 want = (static_cast<u32>(tail[0]) << 24) | (static_cast<u32>(tail[1]) << 16) |
                   (static_cast<u32>(tail[2]) << 8) | static_cast<u32>(tail[3]);
  const u32 got = Adler32(dst, n);
  if (got != want) {
    log_printf(LogLevel::kError, "zlib: Adler-32 不匹配（要 %08X 实得 %08X）",
               static_cast<unsigned>(want), static_cast<unsigned>(got));
    return -1;
  }
  return n;
}

}  // namespace eng
