// engine/base/types.h
// 固定宽度基础类型。纪律：无 iostream/异常/RTTI/std::function/全局构造。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace eng {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

// 数组长度：代替 std::size，不引 <iterator>。
#define ENG_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

// 小工具：自己写而不是引 <algorithm>（那会把一大堆模板拉进来）
template <typename T>
constexpr T Min(T a, T b) {
  return a < b ? a : b;
}
template <typename T>
constexpr T Max(T a, T b) {
  return a > b ? a : b;
}
template <typename T>
constexpr T Clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// ★ 断言 ★：不用异常（-fno-exceptions 已开）。
//   ENG_ASSERT 用于「不可能发生」的状态 —— 直接 abort。
//   ★ 而「运行时资源上限」不许用 abort ★：那种情况要
//     「记录 + 屏显 + 继续运行，不崩、不静默丢」（docs/01 §3 规则 2），
//     走 base/log.h 与 base/pool.h 的记账，不走这里。
#if defined(NDEBUG)
  #define ENG_ASSERT(cond) static_cast<void>(0)
#else
  #define ENG_ASSERT(cond)                       \
    do {                                         \
      if (!(cond)) {                             \
        std::abort();                            \
      }                                          \
    } while (0)
#endif

}  // namespace eng
