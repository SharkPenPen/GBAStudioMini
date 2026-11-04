// engine/platform/platform.h
//
// 平台层唯一入口：窗口、软帧缓冲、输入、计时。
// GBA 版只有一块 240x160 屏幕。
#pragma once

#include "base/log.h"
#include "base/types.h"

namespace eng {

struct PlatformConfig {
  int width = 240;
  int height = 160;
  int window_scale = 3;
  const char* title = "GBAStudioMini";
};

struct SoftFrameBuffer {
  u32* pixels = nullptr;
  int width = 0;
  int height = 0;
  int pitch = 0;
  bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

struct DisplayImage {
  const u32* pixels = nullptr;
  int width = 0;
  int height = 0;
  int pitch = 0;
  bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

// 键位沿用 A/B/Start/Select/方向键；X/Y/L/R 保留给以后。
enum class Key : u8 {
  kA = 0, kB, kX, kY, kL, kR, kStart, kSelect,
  kUp, kDown, kLeft, kRight, kCount,
};
constexpr int kKeyCount = static_cast<int>(Key::kCount);

struct InputState {
  bool down[kKeyCount] = {};
  bool pressed[kKeyCount] = {};
};

inline bool KeyDown(const InputState& in, Key k) {
  return in.down[static_cast<int>(k)];
}
inline bool KeyPressed(const InputState& in, Key k) {
  return in.pressed[static_cast<int>(k)];
}

struct IPlatform {
  bool (*init)(const PlatformConfig& cfg);
  void (*shutdown)();

  void (*begin_frame)();
  void (*end_frame)();

  void (*pump_events)();
  bool (*should_quit)();

  const InputState& (*input)();
  u64 (*tick_us)();

  void (*log_write)(LogLevel lv, const char* text);

  SoftFrameBuffer (*soft_framebuffer)();
  DisplayImage (*read_display)();
};

extern const IPlatform kPlatform;
inline const IPlatform& Platform() { return kPlatform; }

}  // namespace eng
