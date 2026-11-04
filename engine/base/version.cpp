// engine/base/version.cpp
// 与顶层 CMakeLists 的 project(GBAStudioMini VERSION ...) 保持一致。
#include "base/version.h"

namespace eng {

static constexpr u32 kMajor = 0;
static constexpr u32 kMinor = 1;
static constexpr u32 kPatch = 0;

const char* version_string() { return "0.1.0-r0"; }
u32 version_major() { return kMajor; }
u32 version_minor() { return kMinor; }
u32 version_patch() { return kPatch; }

}  // namespace eng
