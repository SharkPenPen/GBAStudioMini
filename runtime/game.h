// runtime/game.h
//
// R4 运行时核心：对象状态机 + 图块碰撞 + 相机 + 场景过渡。
// 不解析 JSON；只读 r4::P3d。
#pragma once

#include <string>
#include <map>

#include "engine/platform/platform.h"
#include "engine/render/draw_list.h"
#include "pgmmv/stats.h"
#include "runtime/p3d.h"

namespace r4 {

struct CheckpointState {
  int object = -1;
  float x = 0.0f;
  float y = 0.0f;
  bool active = false;
};

struct ProjectileState {
  int object = -1;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  float life = 0.0f;
  float hit_cd = 0.0f;
  int damage = 0;
  int parent_scene_object = -1;
  bool destroy_on_hit = true;
  bool destroy_with_parent = false;
  bool from_player = false;
};

struct EnemyState {
  int object = -1;
  int scene_object = -1;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;   // 规则驱动时用
  float vy = 0.0f;
  uint32_t tick = 0;
  float home_x = 0.0f;
  int dir = -1;
  int facing = -1;
  int action = 0;
  int frame = 0;
  float anim_time = 0.0f;
  int hp = 1;
  std::map<std::string, int> attrs;  // 实例属性（规则读写）
  float hit_flash = 0.0f;
  float hit_invuln = 0.0f;
  float attack_timer = 0.0f;
  float attack_hold = 0.0f;
  float attack_hit_cd = 0.0f;
  bool attacking = false;
  bool dying = false;
  bool dead = false;
  int death_action = -1;
};

struct Player {
  int object = -1;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  bool on_ground = false;
  int action = 0;
  int frame = 0;
  float anim_time = 0.0f;
  bool anim_end = false;
  int hp = 100;
  int facing = 1;  // 1 = right, -1 = left
  std::map<std::string, int> attrs;  // 实例属性（规则读写）
};

enum class CameraProfile {
  kFollow,
  kFixed,
};

class Game {
 public:
  bool Init(const P3d& p3d, std::string* error);
  void SetScene(int index);
  void PlaySceneBgm();
  void PlayClipById(const std::string& id);

  void Update(float dt, const eng::InputState& input);
  void Render(eng::DrawList& dl, const eng::TextureId* textures,
              int texture_count) const;

  int scene_index() const { return scene_index_; }
  int UiValue(const char* name) const;
  bool SaveBytes(std::string* out) const;
  bool SaveExists() const;
  void StartNewGame();
  bool LoadBytes(const std::string& data);
  bool SaveToFile(const std::string& path) const;
  bool LoadFromFile(const std::string& path);
  void SetSavePath(const std::string& path) { save_path_ = path; }
  int view_w() const { return view_w_; }
  int view_h() const { return view_h_; }
  int transition_count() const { return transition_count_; }
  int frame_count() const { return frame_count_; }
  float camera_x() const { return camera_x_; }
  float camera_y() const { return camera_y_; }
  const std::string& toast() const { return toast_; }
  const Player& player() const { return player_; }
  const std::vector<EnemyState>& enemies() const { return enemies_; }
  size_t projectile_count() const { return projectiles_.size(); }
  const Scene& scene() const { return p3d_->scenes[scene_index_]; }
  const std::string& scene_id() const { return scene().id; }
  bool finished() const { return finished_; }
  bool UiModalActive() const;
  uint32_t tick() const { return tick_; }

  // 当前帧上某个连接点的世界坐标（没有这个点就返回 false）
  bool PlayerAttachPoint(const char* name, float* x, float* y) const;

 private:
  int ChunkTile(const SceneLayer& layer, int col, int row) const;
  int TileWallsAt(int col, int row) const;
  void DrawToast(eng::DrawList& dl, const eng::TextureId* textures,
                 int texture_count);
  void DrawUiScreens(eng::DrawList& dl, const eng::TextureId* textures,
                     int texture_count) const;
  void OpenUiById(const std::string& id);
  void CloseUi();
  void RequestCloseUi();
  void MoveModalFocus(int dir, bool allow_list);
  void ActivateModalFocus();
  void UpdateInteraction(const eng::InputState& input);
  void RunPlayerRules(const Object& o, const eng::InputState& input, float dt);
  void RunEnemyRules(const Object& o, EnemyState& e, float dt);
  void SpawnById(const std::string& id, float x, float y, int dir, bool from_player);
  void ResolveX(float dx, int obj_index);
  void ResolveY(float dy, int obj_index);
  void UpdateAction(const eng::InputState& input);
  bool CheckCondition(const Condition& c, const eng::InputState& input) const;
  void UpdateAnimation(float dt);
  void UpdateCamera(float dt);
  void Damage();
  void InitEnemies();
  void InitSceneRuntime();
  void UpdateEnemies(float dt);
  void UpdatePlatformer(float dt, const eng::InputState& input);
  void RenderPlatformer(eng::DrawList& dl, const eng::TextureId* textures,
                        int texture_count) const;
  void SpawnProjectiles(const Object& owner, float ox, float oy, float ovx, float ovy,
                        bool from_player, int parent_scene_object);
  void UpdateProjectiles(float dt);
  void RenderProjectiles(eng::DrawList& dl, const eng::TextureId* textures,
                         int texture_count) const;
  void UpdateTriggers();
  bool RunTriggerList(const Trigger& trigger,
                      const std::vector<TriggerAction>& list);

  const P3d* p3d_ = nullptr;
  int scene_index_ = 0;
  int view_w_ = 480;
  int view_h_ = 272;
  int player_object_ = -1;
  Player player_;
  std::vector<EnemyState> enemies_;
  std::vector<ProjectileState> projectiles_;
  std::vector<CheckpointState> checkpoints_;
  std::vector<pgmmv::StatMod> env_mods_;  // 环境修饰器（触发器/装备/Buff 以后往里填）
  float hp_accum_ = 0.0f;                 // hp_rate 的小数累积
  float dir_hold_ = 0.0f;                 // 方向键按住了多久（秒，长按加速用）
  bool debug_on_ = false;                 // Start 切换调试叠加
  bool globals_seeded_ = false;           // 全局变量是否已播种
  int active_interaction_ = -1;           // 当前可交互的触发器下标
  std::vector<uint8_t> interaction_done_; // once 的触发记录
  float respawn_x_ = 0.0f;
  float respawn_y_ = 0.0f;
  bool has_respawn_ = false;
  std::vector<unsigned char> trigger_done_;
  std::vector<unsigned char> trigger_inside_;  // 上一帧是否在区域内（离开检测）
  std::vector<unsigned int> trigger_tick_;     // 在区域内的帧数（持续效果用）
  float player_attack_active_ = 0.0f;
  float player_attack_cooldown_ = 0.0f;
  CameraProfile camera_profile_ = CameraProfile::kFollow;
  std::map<std::string, int> variables_;
  std::string save_path_ = "save0.dat";
  std::vector<int> modal_stack_;
  int modal_focus_ = -1;
  int modal_list_index_ = 0;
  int modal_timer_ = 0;
  int modal_fade_ = 0;
  int modal_fade_total_ = 0;
  bool modal_closing_ = false;
  int modal_close_fade_ = 0;
  int modal_close_total_ = 0;
  bool has_pending_ui_ = false;
  Trigger pending_trigger_;
  std::vector<TriggerAction> pending_actions_;
  float camera_x_ = 0.0f;
  float camera_y_ = 0.0f;
  int transition_count_ = 0;
  int frame_count_ = 0;
  uint32_t tick_ = 0;
  eng::InputState prev_input_{};  // 上一帧输入（key_up 用）
  int key_hold_[eng::kKeyCount] = {};  // 按住帧数（长按用）
  bool finished_ = false;
  float invuln_ = 0.0f;
  std::string toast_;
  float toast_timer_ = 0.0f;
};

}  // namespace r4
