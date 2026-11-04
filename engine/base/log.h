// engine/base/log.h
// 日志：环形缓冲保留最近 N 条；base 不依赖 platform，输出由调用方装 sink。
#pragma once

#include "base/types.h"

namespace eng {

enum class LogLevel : u8 {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

// 日志接收器。level 与 text（已含换行与否由 sink 决定）交给它。
using LogSink = void (*)(void* user, LogLevel level, const char* text);

// 装/卸接收器（nullptr = 只记环形缓冲，不输出）
void log_set_sink(LogSink sink, void* user);

// 低于这个级别的日志被丢弃。默认 kInfo。
void log_set_min_level(LogLevel level);
LogLevel log_min_level();

// 记一条。text 会被复制进环形缓冲，调用方不必保活。
void log_write(LogLevel level, const char* text);

// printf 风格便捷入口（带 GCC 格式检查，格式写错编译期就报）
#if defined(__GNUC__)
  void log_printf(LogLevel level, const char* fmt, ...)
      __attribute__((format(printf, 2, 3)));
#else
  void log_printf(LogLevel level, const char* fmt, ...);
#endif

// ---- 环形缓冲（最近 kLogCapacity 条）----
// ★ i = 0 是最旧的一条 ★；返回 -1 / nullptr 表示越界。
int         log_count();
LogLevel    log_level_at(int i);
const char* log_text_at(int i);

void log_clear();

// 编译期常量，供上层算排版用
constexpr int kLogCapacity = 64;
constexpr int kLogTextMax  = 96;

}  // namespace eng
