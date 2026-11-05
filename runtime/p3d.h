// runtime/p3d.h
// 运行时预编译包 game.p3d（PC 预览用）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <map>
#include <vector>

namespace r4 {

constexpr uint32_t kP3dMagic = 0x31443350u;  // "P3D1" little-endian
constexpr uint32_t kP3dVersion = 76;  // 76: 删掉旧 Scene::UiText
                                      // 75: UI 屏幕 / 元素
                                      // 74: 删掉 SceneLayer.tileset
                                      // 73: 项目视口 view_w/view_h
                                      // 72: 对象可推动标记
                                      // 69: 单屏，移除旧双屏字段与 UI 按钮动作
                                      // 66: 卷轴关卡结束点（scroll_end_x/y + scroll_next）
                                      // 65: 纵版卷轴 scroll_speed_y + 规则 move_add
                                      // 64: 阵营判定统一用 team，删 attack/hurt channel；无敌进属性 invuln
                                      // 63: 攻击力/冷却进属性表（atk / atk_cooldown）
                                      // 62: 分数/弹速/射击间隔都进属性表
                                      // 61: axis/clamp 合成 limit_screen
                                      // 60: 删掉对象 hp（hp 只是属性表里的一条）
                                      // 59: 删掉对象 jump（跳跃由规则给速度）
                                      // 58: 删掉对象旧字段（anim_*/run_*/behavior/patrol/dive）
                                      // 57: 删掉 controller（玩法由相机模式决定）
                                      // 56: 删掉 start_x/start_y（出生点用玩家实例位置）
                                      // 52: 删掉对象 components（运行时从来没读过）
                                      // 51: UI 图片元素
                                      // 49: UI 血条可以引用对象动画（按值显示前 N 帧）
                                      // 48: UI 元素带类型（文字 / 血条）
                                      // 47: 触发器带"在里面时"持续动作
                                      // 46: 触发器带"离开时"动作
                                      // 45: 对象带属性表 + 规则表；文件头带公式库
                                      // 44: 每帧带连接点
                                      // 43: 动作改成每帧一份（图/停留/中心/碰撞/攻击/受击）
                                      // 42: 删掉 tileset.tiles（wall/gimmick/require_*/value 全是死字段）
                                      // 41: 场景图集页表 scene.tilesets + 格子 (槽位,索引) 编码
                                      // 33: 对象加按键→动画映射（anim_left/right/jump/fall/idle）  // 32: 触发器加 name/color/mods（修饰器）
                                      // 31: chunk 改 16x16（旧 32x32 的 p3d 不兼容）
constexpr int kP3dChunkCells = 16;    // 必须与 pgmmv::kSceneChunkCells 一致

struct Image {
  std::string id;
  int width = 0;
  int height = 0;
  std::vector<unsigned char> rgba;  // RGBA8, top-left
};

struct AttackBox {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct AttachPoint {
  std::string name;
  int x = 0;
  int y = 0;
};

// 规则（v45）。type 的取值和编辑器里的枚举一一对应，见 pgmmv/object.h。
struct RuleCond {
  uint8_t type = 0;   // 0 always,1 key_down,2 key_pressed,3 key_up,4 on_ground,
                      // 5 on_ground_not,6 anim_end,7 attr_le,8 var_cmp,9 timer,
                      // 10 player_dist_lt,11 key_held
  std::string key;
  std::string name;
  int op = 1;
  int value = 0;
};

struct RuleEffect {
  uint8_t type = 0;   // 0 move,1 move_px,2 anim,3 flip,4 gravity,5 sfx,6 spawn,
                      // 7 destroy,8 attr_add,9 attr_set,10 damage,11 move_add
  int x = 0;
  int y = 0;
  std::string text;
  std::string formula;
};

struct Rule {
  std::string name;
  bool enabled = true;
  std::vector<RuleCond> when;
  std::vector<RuleEffect> effects;
};

struct ObjectAttr {
  std::string name;
  int value = 0;
};

// v43：一帧一份完整数据（图、停留、中心、碰撞、攻击、受击）。
// 编辑器里的"沿用上一帧"在导出时已经解析成具体值，运行时不用再往前找。
// v44：每帧可以带连接点（枪口/手…），坐标相对帧左上。
struct AnimFrame {
  int image = 0;
  int hold = 0;        // 0 = 按 fps 换算
  int origin_x = 16;   // 相对帧左上
  int origin_y = 16;
  int col_x = 0;       // 碰撞盒（相对中心点）
  int col_y = 0;
  int col_w = 0;
  int col_h = 0;
  AttackBox attack;    // w/h = 0 表示这一帧没有攻击判定
  AttackBox hurt;      // 同上
  std::vector<AttachPoint> attach;  // 连接点
};

struct Action {
  std::string name;
  std::vector<AnimFrame> frames;
  int offset_x = 0;   // 绘制偏移
  int offset_y = 0;
  bool flip_h = false;
  bool flip_v = false;
  int fps = 4;
  bool loop = true;
};

inline int P3dActionHold(const Action& a, size_t index) {
  if (index < a.frames.size() && a.frames[index].hold > 0) {
    return a.frames[index].hold;
  }
  const int fps = a.fps > 0 ? a.fps : 1;
  return (60 + fps / 2) / fps;
}

struct Condition {
  int type = 0;  // 0 key, 1 not_key, 2 on_ground, 3 not_on_ground, 4 anim_end, 5 hp_le_zero, 6 always, 7 timer
  std::string key;
  int value = 0;  // timer ticks
};

struct Link {
  int from = 0;
  int to = 0;
  std::vector<Condition> conditions;
};

struct Spawner {
  int object = -1;
  int offset_x = 0;
  int offset_y = 0;
  float vx = 0.0f;
  float vy = 0.0f;
  int lifetime = 60;
  int count = 1;
  int spread = 0;
  bool inherit_velocity = false;
  bool destroy_on_hit = true;
  bool destroy_with_parent = false;
};

struct Object {
  std::string id;
  std::string name;
  std::string type;
  std::string team;  // v16
  int image = -1;
  int frame_w = 32;
  int frame_h = 32;
  int col_x = -16;
  int col_y = -16;
  int col_w = 32;
  int col_h = 32;
  int hurt_x = 0;  // v17, w/h=0 -> use collision
  int hurt_y = 0;
  int hurt_w = 0;
  int hurt_h = 0;
  int attack_x = -8;
  int attack_y = -8;
  int attack_w = 16;
  int attack_h = 16;
  int speed = 110;   // v1: 原 move；最大速度 px/s
  int gravity = 900; // 竖直恢复 px/s^2
  uint8_t limit_screen = 0;  // v61：移动夹在屏幕内
  uint8_t pushable = 0;      // v72：实体碰撞时可被玩家推动
  std::vector<Action> actions;
  std::vector<Spawner> spawners;
  std::vector<ObjectAttr> attrs;   // v45
  std::vector<Rule> rules;         // v45  // v18
  int default_action = 0;
  std::vector<Link> links;
};

struct Tileset {
  std::string id;
  int image = -1;
  int tile_w = 16;
  int tile_h = 16;
  int columns = 1;
  // v42：没有 tiles[] —— 图块只是一张图里的一个格子，语义在触发器/对象上。
};

struct SceneObject {
  int object = -1;
  int x = 0;
  int y = 0;
  // 运行时状态：被推动后播放的动画（不写入 p3d）
  int action = -1;
  uint32_t anim_start = 0;
};

struct SceneChunk {
  int cx = 0;
  int cy = 0;
  std::vector<int> cells;  // 32*32, -1 empty, 8x8 cells
};

// ★ 格子编码（v41）：高 6 位 = 图集槽位（scene.tilesets 下标），低 20 位 = 页内索引；-1 = 空 ★
//   必须与 pgmmv/scene.h 的 PackCell/CellSlot/CellIndex 一致。
inline int P3dPackCell(int slot, int index) {
  return (slot << 20) | (index & 0xFFFFF);
}
inline int P3dCellSlot(int cell) { return cell < 0 ? -1 : (cell >> 20); }
inline int P3dCellIndex(int cell) { return cell < 0 ? -1 : (cell & 0xFFFFF); }

struct SceneLayer {
  bool visible = true;
  int scroll_x = 100;  // v25
  int scroll_y = 100;
  bool foreground = false;  // v26
  std::vector<SceneChunk> chunks;
};

struct SceneCollisionChunk {
  int cx = 0;
  int cy = 0;
  std::vector<unsigned char> masks;  // 32*32, 0 = empty
};

struct TriggerAction {
  int type = 0;  // 0 change_scene, 1 set_respawn, 2 set_variable, 3 win, 4 lose,
                 // 5 dialogue, 6 play_sfx, 7 attr_add, 8 spawn
  std::string target;
  int value = 0;
};

struct TriggerMod {
  int stat = 0;   // pgmmv::StatId
  int op = 1;     // 0 add / 1 mul / 2 set
  float value = 1.0f;
};

struct TriggerInteraction {
  bool enabled = false;
  std::string prompt;
  std::string ui_id;  // v39：引用 UI 文本
  std::string anchor = "zone_top";
  std::string key;
  bool once = true;
  std::string require_variable;
  int require_value = 0;
  std::vector<TriggerAction> actions;
};

struct Trigger {
  TriggerInteraction interaction;  // v37
  std::string name;            // v32
  uint32_t color = 0x4a90d9ff; // v32
  std::vector<TriggerMod> mods;  // v32：改哪些基础参数
  int x = 0;
  int y = 0;
  int w = 32;
  int h = 32;
  bool once = true;
  int target_object = -1;                     // v71：进入检测的对象索引；-1 = 玩家
  std::string require_variable;
  int require_value = 0;
  std::vector<TriggerAction> actions;         // v46：进入时
  std::vector<TriggerAction> exit_actions;    // v46：离开时
  std::vector<TriggerAction> inside_actions;  // v47：在里面时
  int inside_interval = 30;                   // v47：每 N 帧
};

struct Scene {
  std::string id;
  std::string bgm;  // v30: 背景音乐资源 id
  // v41: 图集页表 —— 槽位（格子高 6 位）→ P3d::tilesets 下标
  std::vector<int> tilesets;
  std::vector<SceneLayer> layers;             // 3 层
  std::vector<SceneCollisionChunk> collision;
  std::vector<SceneObject> objects;

  int gravity = 900;
  std::string camera_mode = "follow";     // follow / fixed / auto_scroll
  int camera_offset_x = 0;  // v22
  int camera_offset_y = 0;
  int camera_deadzone_w = 0;
  int camera_deadzone_h = 0;
  int lives = 3;  // v8
  int scroll_speed = 30;  // v13: horizontal shooter
  int scroll_speed_y = 0; // v65: vertical scroller (positive = content moves down)
  int scroll_end_x = 0;   // v66: scroll this far then switch scene
  int scroll_end_y = 0;
  std::string scroll_next;
  int win_score = 0;      // v67: score target for shooter levels
  std::map<std::string, int> variables;  // v12: scene initial variables
  std::vector<Trigger> triggers;  // v14
  int controlled = -1;  // v27: scene.objects 下标；-1 = 无
  int hud_ui = -1;      // v75: p3d.ui_screens 下标；-1 = 无
};

struct Transition {
  int from = 0;
  int to = 0;
  int type = 0;  // 0 exit, 1 score, 2 switch
  int value_int = 0;
  std::string value_string;
  std::string set_variable;
  int set_value = 0;
};

struct AudioClip {
  std::string id;
  std::vector<unsigned char> data;  // 原始 wav/ogg 字节
};

struct UiBindingData {
  std::string name;
  int map = 0;  // 0 等于 / 1 线性 / 2 步进
  int in_min = 0;
  int in_max = 0;
  int out_min = 0;
  int out_max = 0;
  std::string format;
};

struct UiElementData {
  std::string id;
  int kind = 0;    // 0 panel / 1 text / 2 image / 3 value_image / 4 value_bar
  int anchor = 0;  // 0..8
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int slice_l = 0;
  int slice_r = 0;
  int slice_t = 0;
  int slice_b = 0;
  bool slice_tile = true;
  int image = -1;
  int src_x = 0;
  int src_y = 0;
  int src_w = 0;
  int src_h = 0;
  int frames = 1;
  int frame = 0;
  std::string text;
  int font_size = 10;
  uint32_t color = 0xFFFFFFFFu;
  int align = 0;
  UiBindingData binding;
  std::string visible_var;
  int visible_value = 1;
  int padding = 0;
  std::vector<UiElementData> children;
};

struct UiScreenData {
  std::string id;
  std::string name;
  bool modal = false;
  int layout = 0;  // 0 screen / 1 safe
  std::vector<UiElementData> elements;
};

struct P3d {
  // v45：项目级公式库（名字 → 表达式）
  std::map<std::string, std::string> formulas;
  int initial_scene = 0;
  int view_w = 480;  // v73: 项目逻辑视口宽
  int view_h = 272;  // v73: 项目逻辑视口高
  std::vector<AudioClip> audio;
  std::vector<Image> images;
  std::vector<Object> objects;
  std::vector<Tileset> tilesets;
  std::vector<Scene> scenes;
  std::vector<Transition> transitions;
  // 全局变量：暂不写进 p3d（序列化下一步单独做，避免格式错位）
  std::vector<std::pair<std::string, int>> global_vars;
  int persistent_ui = -1;  // v75: 常驻界面下标
  std::vector<UiScreenData> ui_screens;  // v75
};

bool P3dLoad(const unsigned char* data, size_t size, P3d* out, std::string* error);
bool P3dLoadFile(const std::filesystem::path& path, P3d* out, std::string* error);
bool P3dSave(const std::filesystem::path& path, const P3d& p3d, std::string* error);

// ★ 把「左下原点、y 向上」的场景数据翻成引擎内部的屏幕系（y 向下）★
//   这是全链路唯一一次坐标转换，见 docs/scene-format.md §0。
void MirrorSceneYUp(P3d& p3d, int view_h);

}  // namespace r4
