// editor/theme.cpp
#include "theme.h"

#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace eng {

namespace {

// ★ 键名表由 X-macro 生成 ★ —— 和枚举、默认值同一份定义
const char* const kKeyNames[] = {
#define X(name, def) #name,
    THEME_KEYS(X)
#undef X
};

const u32 kKeyDefaults[] = {
#define X(name, def) def,
    THEME_KEYS(X)
#undef X
};

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// 解析 "#RRGGBB" 或 "RRGGBB"（大小写都收）。返回 0 表示失败。
bool ParseHex6(const char* s, u32* out) {
  if (s == nullptr) {
    return false;
  }
  if (*s == '#') {
    ++s;
  }
  u32 v = 0;
  for (int i = 0; i < 6; ++i) {
    const int h = HexVal(s[i]);
    if (h < 0) {
      return false;
    }
    v = (v << 4) | static_cast<u32>(h);
  }
  // ★ 必须正好 6 位：后面只允许空白/行尾 ★
  for (const char* p = s + 6; *p != '\0'; ++p) {
    if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
      return false;
    }
  }
  *out = v;
  return true;
}

const char* TrimLeft(const char* s) {
  while (*s == ' ' || *s == '\t' || *s == '\r') {
    ++s;
  }
  return s;
}

}  // namespace

const char* ThemeKeyName(ThemeKey k) {
  const int i = static_cast<int>(k);
  return (i >= 0 && i < kThemeKeyCount) ? kKeyNames[i] : "?";
}

bool ThemeKeyFromName(const char* name, ThemeKey* out) {
  if (name == nullptr || out == nullptr) {
    return false;
  }
  for (int i = 0; i < kThemeKeyCount; ++i) {
    if (std::strcmp(kKeyNames[i], name) == 0) {
      *out = static_cast<ThemeKey>(i);
      return true;
    }
  }
  return false;
}

Theme ThemeDefault() {
  Theme t;
  for (int i = 0; i < kThemeKeyCount; ++i) {
    t.c[i] = kKeyDefaults[i];
  }
  return t;
}

// ===========================================================================
bool ThemeLoad(const char* path, Theme* out, ThemeLoadReport* report) {
  if (path == nullptr || out == nullptr) {
    return false;
  }
  ThemeLoadReport local;
  ThemeLoadReport& rep = (report != nullptr) ? *report : local;

  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    return false;
  }

  // ★ 先铺默认值 ★ ⇒ 主题文件只写想改的键就行（这是主题文件该有的样子）
  *out = ThemeDefault();

  char line[256];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    ++rep.lines;

    // 去掉行尾换行
    usize n = std::strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }

    const char* p = TrimLeft(line);
    if (*p == '\0' || *p == '#' || *p == ';') {
      ++rep.comments;
      continue;
    }

    // key = value
    //
    // ★ 不能用 sscanf("%[^= \t]=%s") ★ —— 实测它要求 `=` ★紧跟★键名，
    //   而主题文件是 `canvas_bg    = #FFFFFF`（对齐用的空格）。
    //   结果就是"刚写出来的默认文件，16 个键全被判成坏值"。
    //   ⇒ 手动按 '=' 切，两侧各去空白，行为可预测。
    const char* eq = std::strchr(p, '=');
    if (eq == nullptr) {
      ++rep.bad_values;
      continue;
    }

    // 键：p..eq，去掉尾部空白
    char key[64];
    usize klen = static_cast<usize>(eq - p);
    while (klen > 0 && (p[klen - 1] == ' ' || p[klen - 1] == '\t')) {
      --klen;
    }
    if (klen == 0 || klen >= sizeof(key)) {
      ++rep.bad_values;
      continue;
    }
    std::memcpy(key, p, klen);
    key[klen] = '\0';

    // 值：eq+1 起，去掉前导空白
    const char* vstart = TrimLeft(eq + 1);
    char val[64];
    usize vi = 0;
    while (vstart[vi] != '\0' && vstart[vi] != ' ' && vstart[vi] != '\t' &&
           vi + 1 < sizeof(val)) {
      val[vi] = vstart[vi];
      ++vi;
    }
    val[vi] = '\0';

    ThemeKey k{};
    if (!ThemeKeyFromName(key, &k)) {
      // ★ 未知键：跳过，但记下第一个 ★
      if (rep.unknown_keys == 0) {
        std::snprintf(rep.first_unknown, sizeof(rep.first_unknown), "%s", key);
      }
      ++rep.unknown_keys;
      continue;
    }

    u32 rgb = 0;
    if (!ParseHex6(val, &rgb)) {
      ++rep.bad_values;
      continue;
    }
    out->c[static_cast<int>(k)] = rgb;
    ++rep.applied;
  }
  std::fclose(f);
  return rep.applied > 0;
}

// ===========================================================================
bool ThemeWriteDefault(const char* path) {
  if (path == nullptr) {
    return false;
  }
  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) {
    return false;  // ★ 只读目录等 —— 由调用方明确报出来，不静默 ★
  }
  const Theme t = ThemeDefault();
  std::fprintf(f, "# PocketLoom 主题（语义色）\n");
  std::fprintf(f, "# 格式：键 = #RRGGBB   ★ 未知键与坏值会被跳过（但会计数报告）★\n");
  std::fprintf(f, "# 只写想改的键即可，其余用默认值。\n\n");
  for (int i = 0; i < kThemeKeyCount; ++i) {
    char hex[16];
    ThemeFormatColor(t.c[i], hex, sizeof(hex));
    std::fprintf(f, "%-12s = %s\n", kKeyNames[i], hex);
  }
  std::fclose(f);
  return true;
}

void ThemeFormatColor(u32 rgb, char* out, int cap) {
  if (out == nullptr || cap < 8) {
    return;
  }
  std::snprintf(out, static_cast<usize>(cap), "#%06X", rgb & 0xFFFFFFu);
}

// ===========================================================================
void ThemeApplyToImGui(const Theme& t, ImGuiStyle* style) {
  if (style == nullptr) {
    return;
  }
  // ★ 深色/浅色不是靠"选一个预设"，而是★由 canvas_bg 的亮度推出来★ ★
  //   这样只要改主题文件里的一个键，整套控件配色跟着翻转，
  //   而不是"改了 canvas_bg 但窗口还是深色"那种割裂。
  const u32 bg = t.c[static_cast<int>(ThemeKey::canvas_bg)] & 0xFFFFFFu;
  const u32 r = (bg >> 16) & 0xFFu;
  const u32 g = (bg >> 8) & 0xFFu;
  const u32 b = bg & 0xFFu;
  const u32 lum = (r * 299u + g * 587u + b * 114u) / 1000u;
  const bool light = lum > 128u;

  if (light) {
    ImGui::StyleColorsLight();
  } else {
    ImGui::StyleColorsDark();
  }

  const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(
      IM_COL32((t.c[static_cast<int>(ThemeKey::highlight)] >> 16) & 0xFF,
               (t.c[static_cast<int>(ThemeKey::highlight)] >> 8) & 0xFF,
               t.c[static_cast<int>(ThemeKey::highlight)] & 0xFF, 255));
  // ★ 1.93 的枚举名是 ImGuiCol_TabSelected（旧版叫 ImGuiCol_TabActive）★
  style->Colors[ImGuiCol_TabSelected] = accent;
  style->Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
  style->Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
  style->Colors[ImGuiCol_HeaderActive] = accent;

  const ImVec4 sep = ImGui::ColorConvertU32ToFloat4(
      IM_COL32((t.c[static_cast<int>(ThemeKey::separator)] >> 16) & 0xFF,
               (t.c[static_cast<int>(ThemeKey::separator)] >> 8) & 0xFF,
               t.c[static_cast<int>(ThemeKey::separator)] & 0xFF, 255));
  style->Colors[ImGuiCol_Separator] = sep;
  style->Colors[ImGuiCol_Border] = sep;

  // ★ 视口绘制要用的语义色不在这里用，但它们在 Theme 里，由视口代码自己取 ★
  //   （T4.8 画棋盘格/相机框时会用到 check1/check2/camera/ghost）
}

}  // namespace eng
