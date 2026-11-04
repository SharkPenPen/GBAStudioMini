// editor/theme.h
//
// ★ 主题：语义色 + 主题文件 ★（docs/03 的 T4.3，色板见 docs/02 §3）
//
// 判据：改主题文件生效；imgui.ini 在 exe 旁
//
// ★★ 为什么叫"语义色"而不是"某个控件的颜色"★★
//   键是 `camera`（相机红框）、`ghost`（幽灵预览）、`eye_on`（可见性眼睛）…
//   ★ 不是 `window_bg` / `button_hover` 这种控件名 ★。
//   好处：同一套键能同时喂给 ImGui 样式、视口绘制、日志着色，
//   而且★换主题不用改代码★。
//   v1 的主题文件 `editor-theme.txt` 用的就是这套键，这里★沿用同名键★，
//   于是配色文档、习惯、观感全部连续。
//
// ★ 键表用 X-macro ★：加一个色键 = 表里加一行（枚举/名字/默认值一起生成）。
#pragma once

#include "base/types.h"
// ★ imgui.h 必须在命名空间★外面★引入 ★
//   放进 namespace eng 里会让 ImGui 的 operator new 和一堆 C 库声明
//   全落到 eng:: 里，报 "may not be declared within a namespace"。
#include "imgui.h"

namespace eng {

// ★ 色键表 —— 顺序就是枚举顺序 ★
//   X(键名, 默认值)   默认值来自 docs/02 §3（v1 的值）
#define THEME_KEYS(X)                 \
  X(canvas_bg, 0xFFFFFFu)   /* 画布底 */ \
  X(grid, 0x808080u)        /* 网格 */   \
  X(scene_edge, 0x696969u)  /* 场景边框 */ \
  X(camera, 0xD01818u)      /* 相机红框 */ \
  X(ghost, 0x78AAE6u)       /* 幽灵预览（浅蓝） */ \
  X(select, 0xFF0000u)      /* 选中 */   \
  X(check1, 0xE8E8E8u)      /* 棋盘格 1（透明） */ \
  X(check2, 0xC8C8C8u)      /* 棋盘格 2 */ \
  X(eye_on, 0x2E8B57u)      /* 可见性：开 */ \
  X(eye_off, 0x9A9A9Au)     /* 可见性：关 */ \
  X(alarm, 0xC01818u)       /* 告警 */   \
  X(log_error, 0xC82828u)   /* 日志：错误 */ \
  X(log_warn, 0xBE7800u)    /* 日志：警告 */ \
  X(log_text, 0x3C4048u)    /* 日志：普通 */ \
  X(highlight, 0x0078D7u)   /* 选中高亮（系统蓝） */ \
  X(separator, 0xCDD0D6u)   /* 分隔线 */

enum class ThemeKey : u8 {
#define X(name, def) name,
    THEME_KEYS(X)
#undef X
        kCount
};

constexpr int kThemeKeyCount = static_cast<int>(ThemeKey::kCount);

struct Theme {
  // 0xRRGGBB（与主题文件里的 #RRGGBB 一一对应）
  u32 c[kThemeKeyCount] = {};
};

// 键名（"camera" 等）与序号的双向查表
const char* ThemeKeyName(ThemeKey k);
bool        ThemeKeyFromName(const char* name, ThemeKey* out);

// ★ 默认主题（浅色，对齐 v1 观感）★
Theme ThemeDefault();

// ★ 加载主题文件 ★
//
// ★ 容错行为沿用 v1：★ 未知键跳过、坏值跳过 ★ —— 但★不静默★：
//   跳过了多少、都是哪些，全部记在 report 里，由调用方打出来。
//   静默跳过会让"我改了主题但没生效"变成谜。
struct ThemeLoadReport {
  int lines = 0;
  int applied = 0;
  int unknown_keys = 0;
  int bad_values = 0;
  int comments = 0;
  char first_unknown[32] = {};
};

// 返回 true = 文件读到了并且至少应用了一个键
bool ThemeLoad(const char* path, Theme* out, ThemeLoadReport* report);

// ★ 把默认主题写成文件 ★（文件不存在时用它，让用户有东西可改）
bool ThemeWriteDefault(const char* path);

// ★ 把语义色灌进 ImGui 样式 ★
//   为什么单独一个函数：Theme 是纯数据，而这个函数是唯一碰 ImGui 的地方。
//   （imgui.h 在本文件顶部引入，不在命名空间里。）
void ThemeApplyToImGui(const Theme& t, ImGuiStyle* style);

// 把颜色格式化成 "#RRGGBB"（dump 用）
void ThemeFormatColor(u32 rgb, char* out, int cap);

}  // namespace eng
