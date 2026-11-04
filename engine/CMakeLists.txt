# engine/ —— PC 端安全层
set(ENGINE_SOURCES
  base/log.cpp
  base/mat.cpp
  base/math.cpp
  base/version.cpp

  render/draw_list.cpp
  render/font.cpp
  render/png.cpp
  render/png_decode.cpp
  render/texture.cpp

  render/backend_soft.cpp
  platform/windows/platform_windows.cpp
)

add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
