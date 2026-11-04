// engine/base/version.h
//
// 为什么放 base：
//   启动日志、编辑器「关于」、以及 ★ 两端同帧比对（docs/00 §5 的 L3）★
//   都要用它来断言「两端跑的是同一个引擎版本」。
//   版本不一致时比对结果没有意义，所以这个断言要先于像素比对。
#pragma once

#include "base/types.h"

namespace eng {

// 语义化版本，例如 "0.1.0"。与 CMake 的 project(VERSION) 保持一致。
const char* version_string();

// 版本的三段数值，便于脚本做「不低于」这类判断。
u32 version_major();
u32 version_minor();
u32 version_patch();

}  // namespace eng
