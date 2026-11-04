// engine/render/png_decode.cpp
//
// ★ PNG 解析（在 inflate 之上）★
//
// 支持：8 位、非隔行、颜色类型 0/2/3/4/6（灰度 / RGB / 调色板 / 灰+α / RGBA）。
// ★ 其余明确拒绝并给出原因 ★ —— 不支持就说不支持，不静默出半张图。
//
// ★★ 为什么自己展开成 RGBA，而不是把源格式原样交出去 ★★
//   调用方的路径只有一条就少一类 bug。★ 而"原样交出去"意味着
//   每个使用者都要自己处理 5 种颜色类型 × 位深 × 调色板 ★ ——
//   那正是 v1 那种"某张图颜色不对"的来源。
#include "render/png.h"

#include <cstdio>
#include <cstring>

#include "base/log.h"

namespace eng {

namespace {

// ---- CRC-32（PNG 每个 chunk 都要校验）----
u32 g_crc_table[256];
bool g_crc_ready = false;

void Crc32Init() {
  if (g_crc_ready) {
    return;
  }
  for (u32 n = 0; n < 256; ++n) {
    u32 c = n;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    g_crc_table[n] = c;
  }
  g_crc_ready = true;
}

u32 Be32(const u8* p) {
  return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
         (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

void SetWhy(char* why, int cap, const char* msg) {
  if (why != nullptr && cap > 0) {
    std::snprintf(why, static_cast<usize>(cap), "%s", msg);
  }
  log_printf(LogLevel::kError, "png: %s", msg);
}

void SetWhyF(char* why, int cap, const char* fmt, int a, int b) {
  if (why != nullptr && cap > 0) {
    std::snprintf(why, static_cast<usize>(cap), fmt, a, b);
  }
  log_printf(LogLevel::kError, "png: ");
  log_printf(LogLevel::kError, fmt, a, b);
}

// ★ 按位深从一行里扣出第 x 个样本 ★
//
// ★★ 为什么必须支持低位深（1/2/4 位）★★
//   第一版只支持位深 8，结果 PIL 写的 16 色调色板图是★位深 4★，
//   直接被拒。而★这是个像素艺术引擎★ ——
//   1/2/4 位的调色板 PNG 是这类美术资源的★常态★，不是边角案例。
//   ⇒ 灰度(0) 和调色板(3) 支持 1/2/4/8；RGB/灰+α/RGBA 只有 8 位（规范如此）。
inline int SampleAt(const u8* row, int x, int bit_depth) {
  if (bit_depth == 8) {
    return row[x];
  }
  const int per_byte = 8 / bit_depth;
  const int byte = row[x / per_byte];
  const int shift = 8 - bit_depth * ((x % per_byte) + 1);
  return (byte >> shift) & ((1 << bit_depth) - 1);
}

// Paeth 预测器（RFC2083 §6.4）
inline int Paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = (p > a) ? (p - a) : (a - p);
  const int pb = (p > b) ? (p - b) : (b - p);
  const int pc = (p > c) ? (p - c) : (c - p);
  if (pa <= pb && pa <= pc) {
    return a;
  }
  return (pb <= pc) ? b : c;
}

}  // namespace

// ===========================================================================
bool PngDecode(const u8* data, int size, u8* rgba_buf, int rgba_cap, u8* scratch,
               int scratch_cap, PngImage* out, char* why, int why_cap) {
  if (out != nullptr) {
    *out = PngImage{};
  }
  if (why != nullptr && why_cap > 0) {
    why[0] = '\0';
  }
  if (data == nullptr || rgba_buf == nullptr || scratch == nullptr || out == nullptr) {
    SetWhy(why, why_cap, "参数为空");
    return false;
  }

  // ---- ① 签名 ----
  static const u8 kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (size < 8 + 25 + 12 || std::memcmp(data, kSig, 8) != 0) {
    SetWhy(why, why_cap, "不是 PNG（签名不对或太短）");
    return false;
  }

  // ---- ② 扫 chunk ----
  int width = 0;
  int height = 0;
  int bit_depth = 0;
  int color_type = -1;
  int interlace = 0;
  const u8* plte = nullptr;
  int plte_len = 0;
  int trns_key = -1;  // 灰度/RGB 的透明色（-1 = 没有）
  // ★ 调色板 PNG 的逐项 alpha（tRNS）★ 缺省全不透明
  u8 pal_alpha[256];
  std::memset(pal_alpha, 255, sizeof(pal_alpha));
  int pal_alpha_len = 0;
  // 真彩 PNG 的透明色键（tRNS = 6 字节 RGB）
  int trns_r = -1, trns_g = -1, trns_b = -1;

  // IDAT 是连续的若干块；把它们在 scratch 的后半段拼起来
  const int idat_cap = scratch_cap / 2;
  u8* idat = scratch + (scratch_cap - idat_cap);
  int idat_len = 0;

  int pos = 8;
  bool saw_iend = false;
  while (pos + 12 <= size) {
    const u32 len = Be32(data + pos);
    const u8* type = data + pos + 4;
    if (len > 0x7FFFFFFFu || pos + 12 + static_cast<int>(len) > size) {
      SetWhy(why, why_cap, "chunk 长度越界");
      return false;
    }
    const u8* body = data + pos + 8;
    const u32 crc_want = Be32(body + len);
    // ★ 校验 CRC（type + data）★
    {
      u32 c = 0xFFFFFFFFu;
      Crc32Init();
      for (int i = 0; i < 4 + static_cast<int>(len); ++i) {
        c = g_crc_table[(c ^ type[i]) & 0xFFu] ^ (c >> 8);
      }
      c ^= 0xFFFFFFFFu;
      if (c != crc_want) {
        SetWhy(why, why_cap, "chunk CRC 校验失败（文件损坏）");
        return false;
      }
    }

    if (std::memcmp(type, "IHDR", 4) == 0) {
      if (len != 13) {
        SetWhy(why, why_cap, "IHDR 长度不是 13");
        return false;
      }
      width = static_cast<int>(Be32(body));
      height = static_cast<int>(Be32(body + 4));
      bit_depth = body[8];
      color_type = body[9];
      const int compression = body[10];
      const int filter = body[11];
      interlace = body[12];
      if (compression != 0 || filter != 0) {
        SetWhy(why, why_cap, "压缩方法/滤镜方法不是 0（PNG 只定义了 0）");
        return false;
      }
      if (interlace != 0) {
        SetWhy(why, why_cap, "不支持隔行（Adam7）");
        return false;
      }
      // ★ 位深：灰度/调色板允许 1/2/4/8；RGB/灰+α/RGBA 只允许 8（规范如此）★
      if (color_type == 0 || color_type == 3) {
        if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8) {
          SetWhyF(why, why_cap, "位深 %d 非法（灰度/调色板允许 1/2/4/8，另一个值 %d）",
                  bit_depth, 0);
          return false;
        }
      } else {
        if (bit_depth != 8) {
          SetWhyF(why, why_cap, "位深 %d 不支持（RGB/灰+α/RGBA 只允许 8，另一个值 %d）",
                  bit_depth, 0);
          return false;
        }
      }
      if (color_type != 0 && color_type != 2 && color_type != 3 && color_type != 4 &&
          color_type != 6) {
        SetWhyF(why, why_cap, "颜色类型 %d 非法（合法：0/2/3/4/6，另一个 %d）", color_type,
                0);
        return false;
      }
    } else if (std::memcmp(type, "PLTE", 4) == 0) {
      plte = body;
      plte_len = static_cast<int>(len) / 3;
    } else if (std::memcmp(type, "tRNS", 4) == 0) {
      // 只处理灰度/RGB 的单透明色；调色板的逐项 alpha 也在这里
      if (color_type == 3) {
        // ★ 调色板：tRNS 是"每个调色板项一个 alpha"的表 ★
        //   以前这里什么都不做，透明索引就按调色板颜色画成不透明 ——
        //   表现为"透明区域变成某个颜色"（019.png 就是 palette[0] 的暗红）。
        const int n = static_cast<int>(len < 256 ? len : 256);
        for (int i = 0; i < n; ++i) pal_alpha[i] = body[i];
        pal_alpha_len = n;
      } else if (color_type == 0 && len >= 2) {
        trns_key = static_cast<int>(body[0]) << 8 | body[1];
      } else if (color_type == 2 && len >= 6) {
        trns_r = static_cast<int>(body[0]) << 8 | body[1];
        trns_g = static_cast<int>(body[2]) << 8 | body[3];
        trns_b = static_cast<int>(body[4]) << 8 | body[5];
      }
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      if (idat_len + static_cast<int>(len) > idat_cap) {
        SetWhy(why, why_cap, "IDAT 总长超过 scratch 容量");
        return false;
      }
      std::memcpy(idat + idat_len, body, len);
      idat_len += static_cast<int>(len);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      saw_iend = true;
      break;
    }
    // 其它 chunk（gAMA/tEXt/…）一律忽略 —— 它们不影响像素

    pos += 12 + static_cast<int>(len);
  }

  if (width <= 0 || height <= 0) {
    SetWhy(why, why_cap, "没有有效的 IHDR");
    return false;
  }
  if (!saw_iend) {
    SetWhy(why, why_cap, "没有 IEND（文件被截断？）");
    return false;
  }
  if (idat_len <= 0) {
    SetWhy(why, why_cap, "没有 IDAT（没有像素数据）");
    return false;
  }
  if (color_type == 3 && plte == nullptr) {
    SetWhy(why, why_cap, "调色板 PNG 但没有 PLTE");
    return false;
  }

  // ---- ③ 通道数 ----
  const int channels = (color_type == 0) ? 1 : (color_type == 2) ? 3
                       : (color_type == 3) ? 1 : (color_type == 4) ? 2 : 4;

  // ---- ④ 解压到 scratch 的前半段 ----
  // ★ 每行字节数按位深算（低位深是★位打包★的）★
  const int bits_per_pixel = channels * bit_depth;
  const int row_bytes = (width * bits_per_pixel + 7) / 8;
  const int need = height * (1 + row_bytes);  // 每行 1 字节滤镜 + 数据
  const int raw_cap = scratch_cap - idat_cap;
  if (need > raw_cap) {
    SetWhyF(why, why_cap, "解压后需要 %d 字节，scratch 只给了 %d", need, raw_cap);
    return false;
  }
  const int got = ZlibInflate(idat, idat_len, scratch, raw_cap);
  if (got < need) {
    SetWhyF(why, why_cap, "解压出 %d 字节，期望 %d", got, need);
    return false;
  }

  // ---- ⑤ 逐行反滤镜 ----
  //   ★ bpp = 每像素★字节数★，至少 1 ★
  //   （滤镜是按字节做的；位深 < 8 时一个像素不足 1 字节，按 1 算 —— 规范 §6.2 也是这么说的）
  const int bpp = (bits_per_pixel + 7) / 8;
  u8* prev = nullptr;
  for (int y = 0; y < height; ++y) {
    u8* row = scratch + y * (1 + row_bytes);
    const int ftype = row[0];
    u8* cur = row + 1;
    for (int x = 0; x < row_bytes; ++x) {
      const int a = (x >= bpp) ? cur[x - bpp] : 0;
      const int b = (prev != nullptr) ? prev[x] : 0;
      const int c = (prev != nullptr && x >= bpp) ? prev[x - bpp] : 0;
      int v = cur[x];
      switch (ftype) {
        case 0: break;                                  // None
        case 1: v += a; break;                          // Sub
        case 2: v += b; break;                          // Up
        case 3: v += (a + b) / 2; break;                // Average
        case 4: v += Paeth(a, b, c); break;             // Paeth
        default:
          SetWhyF(why, why_cap, "第 %d 行的滤镜类型 %d 非法", y, ftype);
          return false;
      }
      cur[x] = static_cast<u8>(v & 0xFF);
    }
    prev = cur;
  }

  // ---- ⑥ 展开成 RGBA8 ----
  const int out_need = width * height * 4;
  if (out_need > rgba_cap) {
    SetWhyF(why, why_cap, "RGBA 需要 %d 字节，只给了 %d", out_need, rgba_cap);
    return false;
  }
  for (int y = 0; y < height; ++y) {
    const u8* cur = scratch + y * (1 + row_bytes) + 1;
    u8* dst = rgba_buf + y * width * 4;
    for (int x = 0; x < width; ++x) {
      u8* t = dst + x * 4;
      if (color_type == 0) {  // 灰度
        // ★ 低位深要★缩放到 8 位★（规范：v * 255 / (2^depth - 1)）★
        int g = SampleAt(cur, x, bit_depth);
        if (bit_depth == 1) { g *= 255; }
        else if (bit_depth == 2) { g *= 85; }
        else if (bit_depth == 4) { g *= 17; }
        t[0] = g;
        t[1] = g;
        t[2] = g;
        t[3] = 255;
        if (trns_key >= 0 && static_cast<int>(g) == trns_key) {
          t[3] = 0;
        }
      } else if (color_type == 2) {  // RGB
        t[0] = cur[x * 3 + 0];
        t[1] = cur[x * 3 + 1];
        t[2] = cur[x * 3 + 2];
        t[3] = 255;
        if (trns_r >= 0 && t[0] == trns_r && t[1] == trns_g && t[2] == trns_b) {
          t[3] = 0;
        }
      } else if (color_type == 3) {  // 调色板
        // ★ 调色板索引不做缩放（它就是下标）★
        const int idx = SampleAt(cur, x, bit_depth);
        if (plte != nullptr && idx < plte_len) {
          t[0] = plte[idx * 3 + 0];
          t[1] = plte[idx * 3 + 1];
          t[2] = plte[idx * 3 + 2];
          t[3] = (idx < pal_alpha_len) ? pal_alpha[idx] : 255;
        } else {
          t[0] = t[1] = t[2] = 0;
          t[3] = 255;
        }
      } else if (color_type == 4) {  // 灰 + α
        const u8 g = cur[x * 2 + 0];
        t[0] = g;
        t[1] = g;
        t[2] = g;
        t[3] = cur[x * 2 + 1];
      } else {  // 6 = RGBA
        t[0] = cur[x * 4 + 0];
        t[1] = cur[x * 4 + 1];
        t[2] = cur[x * 4 + 2];
        t[3] = cur[x * 4 + 3];
      }
    }
  }

  out->rgba = rgba_buf;
  out->width = width;
  out->height = height;
  out->src_channels = channels;
  out->src_bit_depth = bit_depth;
  out->src_palette = (color_type == 3);
  return true;
}

}  // namespace eng
