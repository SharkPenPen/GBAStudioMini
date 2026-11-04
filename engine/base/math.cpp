// engine/base/math.cpp
//
// ★ 整个引擎里唯一允许包含 <cmath> 的地方 ★（见 math.h 的说明）。
//   这样头文件保持干净，libstdc++ 那批浮点模板只编进这一个翻译单元。
#include "base/math.h"

#include <cmath>

namespace eng {

f32 Abs(f32 v) { return std::fabs(v); }
f32 Sqrt(f32 v) { return std::sqrt(v); }
f32 Sin(f32 rad) { return std::sin(rad); }
f32 Cos(f32 rad) { return std::cos(rad); }
f32 Tan(f32 rad) { return std::tan(rad); }
f32 Atan2(f32 y, f32 x) { return std::atan2(y, x); }
f32 Floor(f32 v) { return std::floor(v); }
f32 Ceil(f32 v) { return std::ceil(v); }
f32 Round(f32 v) { return std::round(v); }
f32 Pow(f32 base, f32 e) { return std::pow(base, e); }

bool NearEq(f32 a, f32 b, f32 eps) { return std::fabs(a - b) <= eps; }

}  // namespace eng
