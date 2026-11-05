// 基础参数表 + 修饰器合成（见 docs/parameters.md）
// 组件只读"合成后的值"，不知道是谁改的；修饰器只改参数，不描述玩法。
#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace pgmmv {

// 只留运行时真正会读的 3 个：水平速度、重力、下落上限
// （跳跃/加速/摩擦这些以前是 platformer 硬编码用的，行为搬到规则后没人读了）
enum class StatId {
  kSpeed = 0,
  kGravity,
  kMaxFall,
  kCount,
};

enum class StatOp { kAdd = 0, kMul, kSet };

struct StatMod {
  StatId id = StatId::kSpeed;
  StatOp op = StatOp::kMul;
  float value = 1.0f;
};

// 一组参数值（下标 = StatId）
struct StatBlock {
  float v[static_cast<int>(StatId::kCount)] = {};
  float& operator[](StatId id) { return v[static_cast<int>(id)]; }
  float operator[](StatId id) const { return v[static_cast<int>(id)]; }
};

inline const char* StatIdName(StatId id) {
  switch (id) {
    case StatId::kSpeed: return "speed";
    case StatId::kGravity: return "gravity";
    case StatId::kMaxFall: return "max_fall";
    default: return "?";
  }
}

inline bool StatIdFromName(const std::string& n, StatId* out) {
  for (int i = 0; i < static_cast<int>(StatId::kCount); ++i) {
    const StatId id = static_cast<StatId>(i);
    if (n == StatIdName(id)) {
      if (out != nullptr) *out = id;
      return true;
    }
  }
  return false;
}

inline const char* StatOpName(StatOp op) {
  switch (op) {
    case StatOp::kAdd: return "add";
    case StatOp::kMul: return "mul";
    case StatOp::kSet: return "set";
    default: return "?";
  }
}

// 默认值（和 docs/parameters.md 一致）
inline StatBlock DefaultStats() {
  StatBlock s;
  s[StatId::kSpeed] = 110.0f;
  s[StatId::kGravity] = 900.0f;
  s[StatId::kMaxFall] = 600.0f;
  return s;
}

// 合成：先加，再乘，最后覆盖。base 不被修改。
inline StatBlock ComposeStats(const StatBlock& base,
                              const std::vector<StatMod>& mods) {
  StatBlock out = base;
  for (const StatMod& m : mods) {
    if (m.op == StatOp::kAdd) out[m.id] += m.value;
  }
  for (const StatMod& m : mods) {
    if (m.op == StatOp::kMul) out[m.id] *= m.value;
  }
  for (const StatMod& m : mods) {
    if (m.op == StatOp::kSet) out[m.id] = m.value;
  }
  return out;
}

}  // namespace pgmmv
