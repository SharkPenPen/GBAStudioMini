// engine/base/math.h
// 数学声明；只有 math.cpp 引入 <cmath>，避免每个 TU 拉进重模板。
#pragma once

#include "base/types.h"

namespace eng {

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kTau = 6.28318530717958647692f;
constexpr f32 kDegToRad = kPi / 180.0f;
constexpr f32 kRadToDeg = 180.0f / kPi;

// 浮点比较的默认容差。
// ★ 只用于"这两个值算出来应该一样"的场合 ★，不要拿它掩盖真正的精度问题。
constexpr f32 kEpsilon = 1e-6f;

f32 Abs(f32 v);
f32 Sqrt(f32 v);
f32 Sin(f32 rad);
f32 Cos(f32 rad);
f32 Tan(f32 rad);
f32 Atan2(f32 y, f32 x);
f32 Floor(f32 v);
f32 Ceil(f32 v);
f32 Round(f32 v);
f32 Pow(f32 base, f32 e);

constexpr f32 DegToRad(f32 deg) { return deg * kDegToRad; }
constexpr f32 RadToDeg(f32 rad) { return rad * kRadToDeg; }
constexpr f32 Lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
constexpr f32 Sign(f32 v) { return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f); }

bool NearEq(f32 a, f32 b, f32 eps = kEpsilon);

}  // namespace eng
