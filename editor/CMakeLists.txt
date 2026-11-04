# editor/ —— Windows 编辑器（PC 独有）
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
  message(FATAL_ERROR "third_party/imgui missing")
endif()

add_library(imgui STATIC
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends"
)
target_compile_options(imgui PRIVATE -w)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler imm32 dwmapi)

set(EDITOR_SOURCES
  app/main.cpp
  theme.cpp
  ${CMAKE_SOURCE_DIR}/runtime/audio_miniaudio.cpp
)

add_executable(editor ${EDITOR_SOURCES})
set_target_properties(editor PROPERTIES OUTPUT_NAME "gbaedit")

add_executable(editor_gui WIN32 ${EDITOR_SOURCES} winmain.cpp)
set_target_properties(editor_gui PROPERTIES OUTPUT_NAME "GBAStudioMini-Editor")

foreach(_t editor editor_gui)
  target_include_directories(${_t} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_include_directories(${_t} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/audio")
  target_link_libraries(${_t} PRIVATE engine pgmmv imgui comdlg32 shell32 ole32 winmm)
  target_link_options(${_t} PRIVATE -static -static-libgcc -static-libstdc++)
endforeach()
