# runtime/ —— p3d 格式 + 规则运行时 + PC 预览
add_library(p3d_format STATIC p3d.cpp)
target_include_directories(p3d_format PUBLIC "${CMAKE_SOURCE_DIR}")

add_library(runtime_core STATIC game.cpp ui.cpp)
target_link_libraries(runtime_core PUBLIC p3d_format engine_core)

add_executable(game_pc pc_main.cpp audio_miniaudio.cpp)
set_target_properties(game_pc PROPERTIES OUTPUT_NAME "PocketLoom-Play")
target_include_directories(game_pc PRIVATE "${CMAKE_SOURCE_DIR}/third_party/audio")
target_link_libraries(game_pc PRIVATE runtime_core engine_host_pc winmm ole32)
target_link_options(game_pc PRIVATE -static -static-libgcc -static-libstdc++)
