# pgmmv/ —— 编辑器/工具侧的文档与数据层（PC only）
#
# 运行时（PC）不解析 JSON；这里只给编辑器/工具。

add_library(pgmmv STATIC
  json.cpp
  animation.cpp
  object.cpp
  project.cpp
  scene.cpp
  tileset.cpp
  export.cpp
)

# 公开项目根，这样源码/编辑器/测试都统一写 "pgmmv/..."
target_include_directories(pgmmv PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(pgmmv PUBLIC engine_core p3d_format)
