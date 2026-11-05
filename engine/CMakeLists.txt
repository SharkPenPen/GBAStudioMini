# engine/ —— 平台无关核心（engine_core）+ PC 宿主（engine_host_pc）
#
# engine_core：base / draw_list / texture / ui_font / png，不依赖任何 OS。
# engine_host_pc：Windows 窗口、输入、计时、软件光栅。
# PSP/3DS 以后各有一份 host，不放进 core。

set(ENGINE_CORE_SOURCES
  base/log.cpp
  base/mat.cpp
  base/math.cpp
  base/version.cpp

  render/draw_list.cpp
  render/png.cpp
  render/png_decode.cpp
  render/texture.cpp
)

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set(ENGINE_HOST_PC_SOURCES
  render/backend_soft.cpp
  platform/windows/platform_windows.cpp
)

add_library(engine_host_pc STATIC ${ENGINE_HOST_PC_SOURCES})
target_link_libraries(engine_host_pc PUBLIC engine_core winmm)
