#pragma once

#include <string>
#include <vector>

#include "engine/render/draw_list.h"
#include "engine/render/texture.h"

namespace r4 {

struct UiElementData;
struct UiScreenData;

enum class UiAnchor {
  kTopLeft = 0,
  kTopCenter,
  kTopRight,
  kMidLeft,
  kCenter,
  kMidRight,
  kBottomLeft,
  kBottomCenter,
  kBottomRight,
};

enum class UiLayout { kScreen = 0, kSafe };

enum class UiKind {
  kPanel = 0,
  kText,
  kImage,
  kValueImage,
  kValueBar,
  kValueIcons,
  kList,
  kButton,
};

struct UiRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct UiNineSlice {
  int left = 0;
  int right = 0;
  int top = 0;
  int bottom = 0;
  bool tile = true;  // true = 平铺；false = 中间纯色填充
};

struct UiBinding {
  std::string name;    // 变量 / 属性名
  int map = 0;         // 0 等于 / 1 线性 / 2 步进
  int in_min = 0;
  int in_max = 0;
  int out_min = 0;
  int out_max = 0;
  std::string format;  // text 用："%d" / "%02d:%02d"
};

struct UiElement {
  std::string id;
  UiKind kind = UiKind::kPanel;
  UiAnchor anchor = UiAnchor::kTopLeft;
  int x = 0;
  int y = 0;
  int w = 0;  // 0 = 用内容固有尺寸
  int h = 0;

  UiNineSlice slice;
  eng::TextureId texture = 0;
  eng::TexRegion src;

  int frames = 1;
  int frame = 0;
  std::string text;
  int font_size = 12;
  eng::u32 color = 0xFFFFFFFFu;
  int align = 0;  // 0 左 / 1 中 / 2 右

  UiBinding binding;
  std::string visible_var;
  int visible_value = 1;

  int padding = 0;
  std::string action;
  std::string action_target;
  int action_value = 0;
  std::vector<std::string> items;
  int selected = 0;  // runtime list cursor
  std::vector<UiElement> children;  // 只做一层容器
};

struct UiScreen {
  std::string id;
  std::string name;
  bool modal = false;
  UiLayout layout = UiLayout::kScreen;
  int dismiss = 0;  // 0 confirm / 1 cancel / 2 timer / 3 none
  int dismiss_frames = 0;
  int fade_frames = 0;
  std::vector<UiElement> elements;
};

struct UiDrawContext {
  eng::DrawList* dl = nullptr;
  int view_w = 480;
  int view_h = 272;
  int (*value)(void* user, const char* name) = nullptr;
  void* user = nullptr;
  int focus = -1;        // 顶层按钮/列表焦点
  int list_selected = 0; // 当前列表的选中行
};

UiRect UiLayoutRect(UiLayout layout, int view_w, int view_h);

// 锚点 + 偏移 + 尺寸；w/h 为 0 时用 intrinsic_w/h。
UiRect UiAnchorRect(const UiElement& e, UiRect layout, int intrinsic_w,
                    int intrinsic_h);

// 按字体图集测量；spacing 是字间距。
int UiTextWidth(const std::string& text, int font_size, int spacing);

// 自动换行：先按 \n 分段，再按宽度断行。
std::vector<std::string> UiWrapText(const std::string& text, int max_w,
                                    int font_size, int spacing);

// 画一个界面（含一层子元素）。
void UiDrawScreen(const UiScreen& screen, const UiDrawContext& ctx);

// 单行文字（HUD / toast 用）；max_w > 0 时自动换行。
void UiDrawTextLine(eng::DrawList& dl, int x, int y, const std::string& text,
                    eng::u32 color, int align, int max_w);

// p3d 数据 → 运行时元素；image 下标通过 textures 解析。
UiElement UiElementFromData(const UiElementData& d, const eng::TextureId* textures,
                            int texture_count);
UiScreen UiScreenFromData(const UiScreenData& d, const eng::TextureId* textures,
                          int texture_count);

}  // namespace r4
