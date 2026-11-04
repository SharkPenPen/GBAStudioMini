#pragma once

#include <filesystem>
#include <string>

#include "pgmmv/project.h"
#include "pgmmv/scene.h"

namespace pgmmv {

// G1 最小切片：把一个场景的主层烘焙成 GBA Mode 0 用的 C 头文件。
// 限制（第一版）：
//   - 只取 layers[1]（主层）
//   - 只支持一个 8x8 图块组 / 一张图 / 16 色以内
//   - 只导出屏幕左上角 30x20 格（240x160），超出部分后续再做滚动
bool ExportGbaScene(const Project& project, const SceneDoc& scene,
                    const std::filesystem::path& out_header, std::string* error);

}  // namespace pgmmv
