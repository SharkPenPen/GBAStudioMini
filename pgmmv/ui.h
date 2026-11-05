#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pgmmv/json.h"

namespace pgmmv {

// 编辑器侧 UI 元素。运行时数据在 runtime/p3d.h 的 UiElementData。
struct UiElementDoc {
  std::string id;
  std::string kind = "panel";  // panel / text / image / value_image / value_bar
  std::string anchor = "top_left";
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  int slice_l = 0;
  int slice_r = 0;
  int slice_t = 0;
  int slice_b = 0;
  bool slice_tile = true;

  std::string image;  // asset id
  int src_x = 0;
  int src_y = 0;
  int src_w = 0;
  int src_h = 0;
  int frames = 1;
  int frame = 0;

  std::string text;
  int font_size = 12;
  unsigned color = 0xFFFFFFFFu;
  int align = 0;

  std::string value_var;
  int map = 0;
  int in_min = 0;
  int in_max = 0;
  int out_min = 0;
  int out_max = 0;
  std::string format;

  std::string visible_var;
  int visible_value = 1;
  int padding = 0;
  std::vector<UiElementDoc> children;

  JsonValue raw;
};

struct UiScreenDoc {
  std::string id;
  std::string name;
  bool modal = false;
  std::string layout = "screen";  // screen / safe
  std::vector<UiElementDoc> elements;
  JsonValue raw;
};

bool UiScreenLoad(const std::filesystem::path& path, UiScreenDoc* out,
                  std::string* error);
bool UiScreenSave(const std::filesystem::path& path, UiScreenDoc* doc,
                  std::string* error);

}  // namespace pgmmv
