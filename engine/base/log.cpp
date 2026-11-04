// engine/base/log.cpp
#include "base/log.h"

#include <cstdarg>
#include <cstdio>

namespace eng {

namespace {

struct Entry {
  LogLevel level;
  char     text[kLogTextMax];
};

// ★ 这是 POD 数组，不是「全局对象构造」★ —— 它落在 BSS 里被清零，
//   没有构造函数在 main 之前跑。D-03a 禁的是后者。
Entry g_ring[kLogCapacity];

int  g_head = 0;   // 下一个写入位置
int  g_count = 0;  // 已存条数（上限 kLogCapacity）

LogSink  g_sink = nullptr;
void*    g_sink_user = nullptr;
LogLevel g_min = LogLevel::kInfo;

// i = 0 是最旧的一条
const Entry& EntryAt(int i) {
  const int oldest = (g_head - g_count + kLogCapacity) % kLogCapacity;
  return g_ring[(oldest + i) % kLogCapacity];
}

}  // namespace

void log_set_sink(LogSink sink, void* user) {
  g_sink = sink;
  g_sink_user = user;
}

void log_set_min_level(LogLevel level) { g_min = level; }
LogLevel log_min_level() { return g_min; }

void log_write(LogLevel level, const char* text) {
  if (static_cast<u8>(level) < static_cast<u8>(g_min)) {
    return;
  }

  Entry& e = g_ring[g_head];
  e.level = level;

  int i = 0;
  if (text != nullptr) {
    for (; text[i] != '\0' && i < kLogTextMax - 1; ++i) {
      e.text[i] = text[i];
    }
  }
  e.text[i] = '\0';

  g_head = (g_head + 1) % kLogCapacity;
  if (g_count < kLogCapacity) {
    ++g_count;
  }

  if (g_sink != nullptr) {
    g_sink(g_sink_user, level, e.text);
  }
}

void log_printf(LogLevel level, const char* fmt, ...) {
  char buf[kLogTextMax];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log_write(level, buf);
}

int log_count() { return g_count; }

LogLevel log_level_at(int i) {
  if (i < 0 || i >= g_count) {
    return LogLevel::kInfo;
  }
  return EntryAt(i).level;
}

const char* log_text_at(int i) {
  if (i < 0 || i >= g_count) {
    return nullptr;
  }
  return EntryAt(i).text;
}

void log_clear() {
  g_head = 0;
  g_count = 0;
}

}  // namespace eng
