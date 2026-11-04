// pgmmv/export.h
//
// 编辑器 → game.p3d。运行时只认这个文件，不解析 JSON。
#pragma once

#include <filesystem>
#include <string>

#include "pgmmv/project.h"

namespace pgmmv {

bool ExportP3d(const Project& project, const std::filesystem::path& out_path,
               std::string* error);

}  // namespace pgmmv
