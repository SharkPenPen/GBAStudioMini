# tests/ —— 功能验收（不依赖编辑器/工程 JSON/美术）
add_executable(capabilities capabilities.cpp audio_stub.cpp)
target_link_libraries(capabilities PRIVATE runtime_core engine_core pgmmv)
target_include_directories(capabilities PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_options(capabilities PRIVATE -static -static-libgcc -static-libstdc++)
add_test(NAME capabilities COMMAND capabilities)
