// pgmmv/object.h
//
// R2：对象文档（动作动画 + 动作流程）。
//
// 一个对象文件：<project>/objects/<id>.json
// 运行时不解析它；导出阶段再生成 game.p3d。
#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "pgmmv/json.h"

namespace pgmmv {

constexpr int kObjectFormatVersion = 1;

struct ObjectFrame {
  int w = 32;
  int h = 32;
};

struct CollisionBox {
  int x = -16;
  int y = -16;
  int w = 32;
  int h = 32;
};

// 受控者运动参数（v1 五项）。
struct ObjectPhysics {
  int speed = 110;    // px/s：最大速度
  int gravity = 900;  // px/s^2：竖直恢复；0 = 无重力
  bool limit_screen = false;  // 移动时夹在 400x240 屏幕内（射击类用）
};

const char* MotionAxisName(int axis);
bool MotionAxisFromName(const std::string& name, int* out);
const char* MotionClampName(int clamp);
bool MotionClampFromName(const std::string& name, int* out);

// ★ 一帧的完整数据 ★
//   以前的 frames / holds / attack_boxes / hurt_boxes 是四个平行数组，
//   靠下标对齐（还要 Ensure* 去补长度）。现在合成一个结构，加字段不会再错位。
// 盒子的三态：这一帧没设就沿用上一帧；设了"无"就是从这里开始没有
enum class BoxState : unsigned char {
  kInherit = 0,  // 沿用上一帧（没有上一帧就用方向/对象上的默认值）
  kNone = 1,     // 明确没有
  kBox = 2,      // 有，用下面的值
};

struct FrameBox {
  BoxState state = BoxState::kInherit;
  CollisionBox box{0, 0, 0, 0};
};

struct FrameOrigin {
  bool set = false;
  int x = 16;
  int y = 16;
};

// 连接点：挂在某一帧上的命名位置（相对帧左上），比如枪口 / 手 / 脚底
struct AttachPoint {
  std::string name;
  int x = 0;
  int y = 0;
};

struct ActionFrame {
  int image = 0;                        // 图索引（图集里的第几格）
  int hold = 0;                         // 停留 tick；0 = 按 fps 换算
  FrameOrigin origin;                   // 中心；没设 = 沿用上一帧，到方向上的 origin
  FrameBox collision;                   // 碰撞盒
  FrameBox attack;                      // 攻击盒
  FrameBox hurt;                        // 受击盒
  std::vector<AttachPoint> attach;      // 连接点（这一帧有哪些就是哪些）
};

// 一个方向（default / left / right …）。
struct DirectionDef {
  std::string name = "default";  // 方向名；ActionDef 继承时该字段表示动作名
  std::vector<ActionFrame> frames;
  int origin_x = 16;  // 精灵对齐点（像素，相对帧左上）
  int origin_y = 16;
  int offset_x = 0;   // 绘制偏移（像素）
  int offset_y = 0;
  bool flip_h = false;
  bool flip_v = false;
  int fps = 8;
  bool loop = true;
};

// 解析某一帧的盒子：从这一帧往前找最近一个"设了"的帧。
// 都没设 → fallback（碰撞用对象的默认盒，攻击/受击为 nullptr = 无）。
inline const CollisionBox* ResolveFrameBox(const DirectionDef& d, int index,
                                           int which,
                                           const CollisionBox* fallback) {
  static const CollisionBox kNone{0, 0, 0, 0};
  for (int i = std::min(index, static_cast<int>(d.frames.size()) - 1); i >= 0; --i) {
    const FrameBox& fb = which == 0 ? d.frames[static_cast<size_t>(i)].collision
                         : which == 1 ? d.frames[static_cast<size_t>(i)].attack
                                      : d.frames[static_cast<size_t>(i)].hurt;
    if (fb.state == BoxState::kNone) return &kNone;
    if (fb.state == BoxState::kBox) return &fb.box;
  }
  return fallback;
}

// 解析某一帧的中心点：没设就沿用上一帧，都没有就用方向上的 origin。
inline void ResolveFrameOrigin(const DirectionDef& d, int index, int* x, int* y) {
  *x = d.origin_x;
  *y = d.origin_y;
  for (int i = std::min(index, static_cast<int>(d.frames.size()) - 1); i >= 0; --i) {
    const FrameOrigin& o = d.frames[static_cast<size_t>(i)].origin;
    if (o.set) {
      *x = o.x;
      *y = o.y;
      return;
    }
  }
}

// 动作 = 默认方向（本对象字段）+ 额外方向列表。
// 继承让旧代码继续按“动作直接带帧”使用；额外方向在 directions 里。
struct ActionDef : DirectionDef {
  // 第三层“变体/方向”的名字只用于编辑器/选择器；action.name 仍是动作名。
  // 旧数据没有这个字段时默认“默认”，不强制叫上下左右。
  std::string base_variant = "默认";
  std::vector<DirectionDef> directions;  // 额外变体；name 完全自定义
};

inline DirectionDef* ActionDirectionAt(ActionDef* a, int index) {
  if (a == nullptr) return nullptr;
  if (index <= 0 || index > static_cast<int>(a->directions.size())) {
    return static_cast<DirectionDef*>(a);
  }
  return &a->directions[static_cast<size_t>(index - 1)];
}

inline const DirectionDef* ActionDirectionAt(const ActionDef* a, int index) {
  if (a == nullptr) return nullptr;
  if (index <= 0 || index > static_cast<int>(a->directions.size())) {
    return static_cast<const DirectionDef*>(a);
  }
  return &a->directions[static_cast<size_t>(index - 1)];
}

bool ReadDirectionForResource(const JsonValue& v, DirectionDef* d, std::string* error);
JsonValue WriteDirectionForResource(const DirectionDef& d);

inline int ActionDefaultHold(const DirectionDef& a) {
  // ★ 默认 1 tick（= 1/60 秒）★
  //   以前默认按 60/fps 算，fps=4 时就是 15 tick/帧，新建动作慢得像幻灯片。
  (void)a;
  return 1;
}

inline int ActionHoldAt(const DirectionDef& a, size_t index) {
  if (index < a.frames.size() && a.frames[index].hold > 0) {
    return a.frames[index].hold;
  }
  return ActionDefaultHold(a);
}

inline void EnsureActionHolds(DirectionDef* a) {
  if (a == nullptr) return;
  for (ActionFrame& f : a->frames) {
    if (f.hold != 0) f.hold = std::max(1, std::min(600, f.hold));
  }
}

enum class ConditionType {
  kKey = 0,
  kNotKey,
  kOnGround,
  kNotOnGround,
  kAnimEnd,
  kHpLeZero,
  kAlways,
  kTimer,
  kScoreGe,
  kLivesLe,
  kVariable,
};

const char* ConditionTypeName(ConditionType t);
bool ConditionTypeFromName(const std::string& name, ConditionType* out);

struct FlowCondition {
  ConditionType type = ConditionType::kKey;
  std::string key;
  int value = 0;  // 计时条件：tick
};

struct FlowLink {
  std::string from;
  std::string to;
  std::vector<FlowCondition> conditions;
};

struct SpawnerDef {
  std::string object;
  int offset_x = 0;
  int offset_y = 0;
  float vx = 0.0f;
  float vy = 0.0f;
  int lifetime = 60;  // tick
  int count = 1;
  int spread = 0;     // 角度扩散
  bool inherit_velocity = false;
  bool destroy_on_hit = true;
  bool destroy_with_parent = false;
};



// ---- 规则：条件 + 动作 ----
enum class RuleCond : unsigned char {
  kAlways = 0,
  kKeyDown,
  kKeyPressed,
  kKeyUp,
  kOnGround,
  kOnGroundNot,
  kAnimEnd,
  kAttrLe,
  kVarCmp,
  kTimer,
  kPlayerDistLt,
  kKeyHeld,  // 按住某键超过 N tick（长按）
  kPushable, // 被玩家实体推动时触发（同时声明该对象可推动）
};

enum class RuleAct : unsigned char {
  kMove = 0,   // 设速度（px/s）
  kMovePx,     // 每帧位移（px）
  kAnim,       // 切动作
  kFlip,       // 朝向
  kGravity,    // 施加重力
  kSfx,        // 音效
  kSpawn,      // 生成对象
  kDestroy,    // 销毁自己
  kAttrAdd,    // 属性加减
  kAttrSet,    // 属性赋值
  kDamage,     // 造成伤害（公式）
  kMoveAdd,    // 速度叠加（惯性/击退；不会被覆盖）
  kMoveInput,  // 按方向键移动（俯视角用：自动归一化，斜向不加速）
};

struct AttrDef {
  std::string name;
  int value = 0;
};

struct Cond {
  RuleCond type = RuleCond::kAlways;
  std::string key;    // 按键语义名
  std::string name;   // 属性名 / 变量名
  int op = 1;         // 0:< 1:<= 2:= 3:>= 4:>
  int value = 0;      // 比较值 / 帧间隔 / 像素
};

struct Effect {
  RuleAct type = RuleAct::kMove;
  int x = 0;
  int y = 0;
  std::string text;     // 动画名 / 音效名 / 属性名 / 对象 id
  std::string formula;  // 伤害公式名
};

struct Rule {
  std::string name;
  bool enabled = true;
  std::vector<Cond> when;
  std::vector<Effect> effects;
};

struct ObjectDoc {
  int format_version = kObjectFormatVersion;
  std::string id;
  std::string name;
  std::string type = "player";  // player / enemy / bullet
  std::string image;            // 相对路径，例如 assets/player.png
  ObjectFrame frame;
  CollisionBox collision;
  CollisionBox hurt_box{0, 0, 0, 0};  // w/h=0 时使用 collision
  CollisionBox attack_box{-8, -8, 16, 16};
  std::string team;  // player / enemy / neutral；空 = 按 type 推断
  bool pushable = false;  // 实体碰撞时能否被玩家推动
  ObjectPhysics physics;
  std::vector<AttrDef> attrs;            // 属性表：hp / def / atk …
  std::vector<Rule> rules;               // 规则表
  // 动作不在对象里：对象只引用一个动画资源（animation），动作在 animations/*.json
  std::string animation;
  std::vector<SpawnerDef> spawners;
  std::string default_action = "idle";
  std::vector<FlowLink> links;
  JsonValue raw;
};

// 新对象：四个动作 + 一组最小默认链接。
ObjectDoc ObjectCreateDefault(const std::string& id, const std::string& name);

bool ObjectLoad(const std::filesystem::path& path, ObjectDoc* out, std::string* error);

// 写对象 JSON；不负责创建目录（ProjectSave 会创建 objects/）。
bool ObjectSave(const std::filesystem::path& path, ObjectDoc* obj, std::string* error);

// 把 typed 字段同步到 raw（保留顶层未知字段）。
void ObjectSyncRaw(ObjectDoc* obj);

// id 的 ASCII 安全化（项目内唯一性由调用方保证）。
std::string SanitizeObjectId(const std::string& name);

}  // namespace pgmmv
