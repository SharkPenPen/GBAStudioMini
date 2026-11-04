// pgmmv/tileset.h
//
// R3：图块组文档。标准图块 + 四边墙标记。
// 文件：<project>/tiles/<id>.json
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pgmmv/json.h"

namespace pgmmv {

// 2: 删掉 tiles[]（wall / gimmick / kind / require_* / value 全是上一代玩法，
//    运行时早就不读了）。老文件里还带着的 tiles 字段加载时忽略、保存时丢掉。
constexpr int kTilesetFormatVersion = 2;

struct TilesetDoc {
  int format_version = kTilesetFormatVersion;
  std::string id;
  std::string name;
  std::string image;  // 相对路径
  int tile_w = 16;
  int tile_h = 16;
  int columns = 1;
  int offset_x = 0;  // 图块网格在素材里的起点（框选起点）
  int offset_y = 0;
  JsonValue raw;
};

std::string SanitizeDocId(const std::string& name);

TilesetDoc TilesetCreateDefault(const std::string& id, const std::string& name,
                                const std::string& image, int tile_w, int tile_h,
                                int columns);
bool TilesetLoad(const std::filesystem::path& path, TilesetDoc* out,
                 std::string* error);
bool TilesetSave(const std::filesystem::path& path, TilesetDoc* doc,
                 std::string* error);
void TilesetSyncRaw(TilesetDoc* doc);

}  // namespace pgmmv
