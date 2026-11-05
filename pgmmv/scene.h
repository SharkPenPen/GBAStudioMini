// pgmmv/scene.h
//
// R3：无边界场景 + 独立碰撞层 + 场景过渡。
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "pgmmv/json.h"
#include "pgmmv/stats.h"

namespace pgmmv {

constexpr int kSceneFormatVersion = 6;  // 6: 固定 BG0..BG3 四层 + priority/parallax
constexpr int kSceneBase = 8;
constexpr int kSceneChunkCells = 16;  // 2026-10-28: 32 -> 16（128px，块线不再稀）
constexpr int kSceneLayerCount = 4;  // BG0 HUD / BG1 前景 / BG2 主层 / BG3 背景

struct SceneObject {
  std::string id;    // 实例 id（场景内唯一）；编辑器生成，用于受控绑定
  std::string object;
  int x = 0;
  int y = 0;
};

struct SceneCamera {
  std::string follow = "player";
  int offset_x = 0;
  int offset_y = 0;
  int deadzone_w = 0;
  int deadzone_h = 0;
};

struct TriggerAction {
  std::string type = "change_scene";
  std::string target;
  int value = 0;
};

// 交互点：进区域显示提示，按键执行动作（门/桌子/NPC）
struct TriggerRuleDef {
  bool enabled = true;
  int mode = 0;  // 0 enter / 1 exit / 2 inside
  std::string require_variable;
  int require_value = 0;
  std::vector<TriggerAction> actions;
};

struct TriggerInteraction {
  bool enabled = false;
  std::string prompt;              // 提示文字（备用）
  std::string ui_id;               // 引用 UI 文本 id（优先于 prompt）
  std::string anchor = "zone_top"; // zone_top / zone_center / object
  std::string key;                 // 按键名（b / a / start…）
  bool once = true;
  std::string require_variable;    // 前置条件（空 = 无）
  int require_value = 0;
  std::vector<TriggerAction> actions;
};

struct TriggerDef {
  std::string name;              // 触发器名（水/毒/门…）
  unsigned color = 0x4a90d9ff;   // 显示颜色 0xRRGGBBAA
  std::vector<StatMod> mods;     // 修饰器：改哪些基础参数
  TriggerInteraction interaction;  // 交互点
  int x = 0;
  int y = 0;
  int w = 32;
  int h = 32;
  bool once = true;
  std::string target_object;  // 进入检测的对象 id；空 = 旧行为（玩家）
  std::string require_variable;
  int require_value = 0;
  int inside_interval = 30;                   // 停留规则每 N 帧
  std::vector<TriggerRuleDef> rules;          // 区域规则
};

struct SceneChunk {
  int cx = 0;
  int cy = 0;
  std::vector<int> cells;  // 32*32, -1 = empty (visual tile index)
};

// ★ 格子编码：高 6 位 = 图集槽位（scene.tilesets 下标），低 20 位 = 图块索引 ★
//   -1 = 空。只存裸索引的老数据等价于槽位 0，不需要迁移。
inline int PackCell(int slot, int index) {
  return (slot << 20) | (index & 0xFFFFF);
}
inline int CellSlot(int cell) { return cell < 0 ? -1 : (cell >> 20); }
inline int CellIndex(int cell) { return cell < 0 ? -1 : (cell & 0xFFFFF); }

struct SceneLayer {
  std::string name;
  bool enabled = true;
  bool visible = true;
  bool locked = false;  // 编辑器只读：运行时仍加载
  int priority = 0;     // 0 最前 .. 3 最后
  int parallax_x = 100; // 相机滚动百分比；100 = 跟随世界
  int parallax_y = 100;
  std::map<std::pair<int, int>, SceneChunk> chunks;
};

// 独立碰撞层：每格一个 mask。bit0 上, bit1 右, bit2 下, bit3 左,
// bit4 单向平台。
struct SceneCollision {
  std::map<std::pair<int, int>, SceneChunk> chunks;
  int MaskAt(int col, int row) const;
  void SetMaskAt(int col, int row, int mask);
};

struct SceneDoc {
  int format_version = kSceneFormatVersion;
  std::string id;
  std::string name;
  std::vector<std::string> tilesets;  // 本关用到的图块组；下标 = 格子里的槽位
  std::vector<SceneLayer> layers;  // 固定 4 层（BG0..BG3）
  int map_w = 64;  // 地图格数，最大 64（512px）
  int map_h = 32;
  SceneCollision collision;
  std::vector<SceneObject> objects;
  SceneCamera camera;
  int gravity = 900;
  std::string bgm;  // 背景音乐资源 id  // platformer / free2d
  std::string camera_mode = "follow";     // follow / fixed / auto_scroll
  int lives = 3;
  int scroll_speed = 30;    // 横版射击自动卷轴 px/s
  int scroll_speed_y = 0;   // 纵版：正值 = 画面内容向下滚 px/s
  int scroll_end_x = 0;     // 卷轴关卡：横向卷过这么多像素就切场景（0 = 不切）
  int scroll_end_y = 0;     // 纵向卷轴版
  std::string scroll_next;  // 切到哪个场景（空 = 不切）
  bool objects_locked = false;  // 编辑器只读：对象层锁定
  std::map<std::string, int> variables;  // 场景初始变量
  std::string hud_ui;  // 引用工程里的 UiScreenDoc id；空 = 无 HUD
  std::vector<TriggerDef> triggers;
  std::vector<std::string> controlled;  // 受控实例 id；v1 只用第一个
  JsonValue raw;

  SceneObject* FindSceneObject(const std::string& instance_id);
  const SceneObject* FindSceneObject(const std::string& instance_id) const;
  SceneObject& AddSceneObject(const std::string& object_id, int x, int y);
  void SetControlled(const std::string& instance_id);  // 空字符串 = 清空
  void EnsureSceneObjectIds();
  void EnsureLayers();
  SceneChunk* FindChunk(int layer, int cx, int cy);
  const SceneChunk* FindChunk(int layer, int cx, int cy) const;
  int TileAt(int layer, int col, int row) const;
  void SetTileAt(int layer, int col, int row, int value);
  bool ContentBounds(int* min_col, int* min_row, int* max_col,
                     int* max_row) const;
};

enum class TransitionType { kExit = 0, kScore, kSwitch };
const char* TransitionTypeName(TransitionType t);
bool TransitionTypeFromName(const std::string& name, TransitionType* out);

struct Transition {
  std::string from;
  std::string to;
  TransitionType type = TransitionType::kExit;
  int value_int = 0;
  std::string value_string;
  std::string set_variable;
  int set_value = 0;
};

SceneDoc SceneCreateDefault(const std::string& id, const std::string& name,
                            const std::string& tileset);
bool SceneLoad(const std::filesystem::path& path, SceneDoc* out,
               std::string* error);
bool SceneSave(const std::filesystem::path& path, SceneDoc* doc,
               std::string* error);
void SceneSyncRaw(SceneDoc* doc);

bool TransitionsLoad(const std::filesystem::path& path,
                     std::vector<Transition>* out, std::string* error);
bool TransitionsSave(const std::filesystem::path& path,
                     const std::vector<Transition>& list, std::string* error);

}  // namespace pgmmv
