# ==============================================================================
# cmake/check-include-deps.cmake —— 依赖方向检查（脚本模式）
#
# 用法：cmake -DROOT=<项目根> -P cmake/check-include-deps.cmake
#
# 为什么要有它（docs/01 §1 硬规则）：
#   v1 实测踩过"引擎反向依赖编辑器"——engine 里 include 了 editor 的 util。
#   后果是目标端从来没有真正编译过，而 Windows 侧的 24 项检查全绿。
#   这种错"靠人记得跑脚本"是防不住的，必须在构建里强制。
# ==============================================================================

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "需要 -DROOT=<项目根>")
endif()

set(_engine "${ROOT}/engine")
if(NOT EXISTS "${_engine}")
  message(STATUS "依赖方向：engine/ 还不存在，跳过")
  return()
endif()

file(GLOB_RECURSE _srcs
     "${_engine}/*.c"   "${_engine}/*.h"
     "${_engine}/*.cpp" "${_engine}/*.hpp")

list(LENGTH _srcs _n)
if(_n EQUAL 0)
  message(STATUS "依赖方向：engine/ 里还没有源文件，跳过")
  return()
endif()

set(_bad "")
foreach(_f IN LISTS _srcs)
  file(READ "${_f}" _txt)
  # 命中 #include "editor/..." 或 #include <editor/...>
  if(_txt MATCHES "#[ \t]*include[ \t]*[\"<]editor/")
    list(APPEND _bad "${_f}")
  endif()
endforeach()

if(_bad)
  string(REPLACE ";" "\n  " _badlist "${_bad}")
  message(FATAL_ERROR
    "★ 依赖方向违规：engine/ 不许包含 editor/ ★\n"
    "命中文件：\n  ${_badlist}\n"
    "规则见 docs/01-架构与代码方案.md §1 —— engine 必须能脱离编辑器独立编译。")
endif()

message(STATUS "依赖方向 OK（扫了 ${_n} 个文件）")
