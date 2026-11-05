#include <filesystem>
#include <fstream>
// runtime/game.cpp
#include "runtime/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "engine/base/rect.h"
#include "engine/render/texture.h"
#include "runtime/ui.h"
#include "runtime/audio.h"
#include "runtime/expr.h"

namespace r4 {
namespace {
// 对象属性读成整数（没配就用默认值）：分数 / 弹速 / 射击间隔都走属性表
int ObjIntAttr(const Object& o, const char* name, int def) {
  for (const ObjectAttr& a : o.attrs) {
    if (a.name == name) return a.value;
  }
  return def;
}


void PutU16(std::string* b, unsigned v) {
  b->push_back(static_cast<char>(v & 0xFF));
  b->push_back(static_cast<char>((v >> 8) & 0xFF));
}

void PutI32(std::string* b, int v) {
  const unsigned u = static_cast<unsigned>(v);
  for (int i = 0; i < 4; ++i) {
    b->push_back(static_cast<char>((u >> (i * 8)) & 0xFF));
  }
}

void PutStr(std::string* b, const std::string& s) {
  const unsigned n = s.size() > 65535 ? 65535 : static_cast<unsigned>(s.size());
  PutU16(b, n);
  b->append(s.data(), n);
}

bool GetU16(const std::string& b, size_t* pos, unsigned* out) {
  if (*pos + 2 > b.size()) return false;
  *out = static_cast<unsigned char>(b[*pos]) |
         (static_cast<unsigned>(static_cast<unsigned char>(b[*pos + 1])) << 8);
  *pos += 2;
  return true;
}

bool GetI32(const std::string& b, size_t* pos, int* out) {
  if (*pos + 4 > b.size()) return false;
  unsigned u = 0;
  for (int i = 0; i < 4; ++i) {
    u |= static_cast<unsigned>(static_cast<unsigned char>(b[*pos + i])) << (i * 8);
  }
  *out = static_cast<int>(u);
  *pos += 4;
  return true;
}

bool GetStr(const std::string& b, size_t* pos, std::string* out) {
  unsigned n = 0;
  if (!GetU16(b, pos, &n)) return false;
  if (*pos + n > b.size()) return false;
  out->assign(b.data() + *pos, n);
  *pos += n;
  return true;
}

std::string ObjectTeam(const Object& o) {
  if (!o.team.empty()) return o.team;
  if (o.type == "player") return "player";
  if (o.type == "enemy") return "enemy";
  return "neutral";
}

// v43：碰撞盒是每帧的；这一帧没给（w/h=0）就退回对象上的默认盒。
AttackBox ColBoxOf(const Object& o, int action, int frame) {
  if (action >= 0 && action < static_cast<int>(o.actions.size())) {
    const Action& a = o.actions[static_cast<size_t>(action)];
    if (frame >= 0 && frame < static_cast<int>(a.frames.size())) {
      const AnimFrame& f = a.frames[static_cast<size_t>(frame)];
      if (f.col_w > 0 && f.col_h > 0) {
        return AttackBox{f.col_x, f.col_y, f.col_w, f.col_h};
      }
    }
  }
  return AttackBox{o.col_x, o.col_y, o.col_w, o.col_h};
}

int FindActionIndex(const Object& o, const char* name) {
  for (int i = 0; i < static_cast<int>(o.actions.size()); ++i) {
    if (o.actions[i].name == name) return i;
  }
  return -1;
}
}


namespace {


int ActionTotalTicks(const Action& a) {
  int total = 0;
  for (size_t i = 0; i < a.frames.size(); ++i) total += P3dActionHold(a, i);
  return total;
}

int ActionFrameAt(const Action& a, float time, bool* ended) {
  if (ended != nullptr) *ended = false;
  const int n = static_cast<int>(a.frames.size());
  if (n <= 0) {
    if (ended != nullptr) *ended = true;
    return 0;
  }
  const int total = ActionTotalTicks(a);
  if (total <= 0) return 0;
  int tick = static_cast<int>(std::floor(std::max(0.0f, time) * 60.0f));
  if (a.loop) {
    tick %= total;
  } else if (tick >= total) {
    if (ended != nullptr) *ended = true;
    return n - 1;
  }
  int idx = 0;
  while (idx < n) {
    const int hold = P3dActionHold(a, idx);
    if (tick < hold) break;
    tick -= hold;
    ++idx;
  }
  if (idx >= n) idx = a.loop ? 0 : n - 1;
  return idx;
}

const Object* FindPlayerObject(const P3d& p, const Scene& s, int* out_index) {
  *out_index = -1;
  int idx = s.controlled;
  if (idx < 0 || idx >= static_cast<int>(s.objects.size())) {
    // 兜底：场景没标 controlled 时，找第一个 type=player 的实例
    for (size_t i = 0; i < s.objects.size(); ++i) {
      const int oi = s.objects[i].object;
      if (oi >= 0 && oi < static_cast<int>(p.objects.size()) &&
          p.objects[static_cast<size_t>(oi)].type == "player") {
        idx = static_cast<int>(i);
        break;
      }
    }
  }
  if (idx < 0 || idx >= static_cast<int>(s.objects.size())) return nullptr;
  const int object_index = s.objects[static_cast<size_t>(idx)].object;
  if (object_index < 0 || object_index >= static_cast<int>(p.objects.size())) {
    return nullptr;
  }
  *out_index = object_index;
  return &p.objects[object_index];
}

}  // namespace

bool Game::Init(const P3d& p3d, std::string* error) {
  if (p3d.scenes.empty()) {
    if (error != nullptr) *error = "runtime: no scenes";
    return false;
  }
  p3d_ = &p3d;
  view_w_ = p3d.view_w > 0 ? p3d.view_w : 480;
  view_h_ = p3d.view_h > 0 ? p3d.view_h : 272;
  scene_index_ = p3d.initial_scene;
  if (scene_index_ < 0 || scene_index_ >= static_cast<int>(p3d.scenes.size())) {
    scene_index_ = 0;
  }
  player_object_ = -1;
  score_ = 0;
  toast_.clear();
  toast_timer_ = 0.0f;
  SetScene(scene_index_);
  if (p3d.title_ui >= 0 &&
      p3d.title_ui < static_cast<int>(p3d.ui_screens.size())) {
    OpenUiById(p3d.ui_screens[static_cast<size_t>(p3d.title_ui)].id);
  }
  return true;
}

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

void Game::PlaySceneBgm() {
  if (p3d_ == nullptr) return;
  const Scene& s = p3d_->scenes[scene_index_];
  if (s.bgm.empty()) {
    AudioStop();
    return;
  }
  for (const AudioClip& a : p3d_->audio) {
    if (a.id == s.bgm && !a.data.empty()) {
      AudioPlayClip(a.data.data(), a.data.size(), true);
      return;
    }
  }
  AudioStop();
}

void Game::PlayClipById(const std::string& id) {
  if (id.empty() || p3d_ == nullptr) return;
  for (const AudioClip& a : p3d_->audio) {
    if (a.id == id && !a.data.empty()) {
      AudioPlayClip(a.data.data(), a.data.size(), false);
      return;
    }
  }
}

void Game::SetScene(int index) {
  if (p3d_ == nullptr) return;
  if (index < 0 || index >= static_cast<int>(p3d_->scenes.size())) return;
  modal_stack_.clear();
  modal_focus_ = -1;
  modal_timer_ = 0;
  modal_closing_ = false;
  modal_close_fade_ = 0;
  modal_close_total_ = 0;
  has_pending_ui_ = false;
  pending_actions_.clear();
  scene_index_ = index;
  const Scene& s = p3d_->scenes[index];
  // 场景级变量重置；全局（g: 前缀）保留
  for (auto it = variables_.begin(); it != variables_.end();) {
    if (it->first.rfind("g:", 0) == 0) ++it;
    else it = variables_.erase(it);
  }
  for (const auto& entry : s.variables) variables_[entry.first] = entry.second;
  // 首次进入时播种全局变量
  if (!globals_seeded_ && p3d_ != nullptr) {
    globals_seeded_ = true;
    for (const auto& kv : p3d_->global_vars) variables_["g:" + kv.first] = kv.second;
  }
  // ★ 出生点 = 场景里那个"玩家对象实例"的位置 ★
  //   不再用场景的 start_x/start_y：那是和实例位置重复的一份数据，
  //   拖动实例却改不了出生点，两个值迟早对不上。
  int spawn_x = 64;
  int spawn_y = 192;
  auto instance_of = [&](int object_index, int* ox, int* oy) {
    for (const SceneObject& inst : s.objects) {
      if (inst.object != object_index) continue;
      *ox = inst.x;
      *oy = inst.y;
      return true;
    }
    return false;
  };
  const bool controlled_is_player =
      s.controlled >= 0 && s.controlled < static_cast<int>(p3d_->objects.size()) &&
      p3d_->objects[static_cast<size_t>(s.controlled)].type == "player";
  if (!controlled_is_player || !instance_of(s.controlled, &spawn_x, &spawn_y)) {
    bool found = false;
    for (const SceneObject& inst : s.objects) {
      if (inst.object < 0 || inst.object >= static_cast<int>(p3d_->objects.size())) continue;
      if (p3d_->objects[static_cast<size_t>(inst.object)].type != "player") continue;
      spawn_x = inst.x;
      spawn_y = inst.y;
      found = true;
      break;
    }
    if (!found && !s.objects.empty()) {
      spawn_x = s.objects.front().x;
      spawn_y = s.objects.front().y;
    }
  }
  respawn_x_ = static_cast<float>(spawn_x);
  respawn_y_ = static_cast<float>(spawn_y);
  has_respawn_ = false;
  trigger_done_.assign(s.triggers.size(), 0);
  player_.x = static_cast<float>(spawn_x);
  player_.y = static_cast<float>(spawn_y);
  player_.vx = 0.0f;
  player_.vy = 0.0f;
  player_.on_ground = false;
  player_.anim_time = 0.0f;
  player_.frame = 0;
  player_.anim_end = false;
  player_.hp = 100;
  player_.action = 0;
  player_.facing = 1;
  won_ = false;
  finished_ = false;
  invuln_ = 0.0f;

  int player_index = -1;
  const Object* po = FindPlayerObject(*p3d_, s, &player_index);
  if (po != nullptr) {
    player_object_ = player_index;
    player_.action = po->default_action;
    player_.attrs.clear();
    for (const ObjectAttr& a : po->attrs) player_.attrs[a.name] = a.value;
    if (player_.attrs.find("hp") == player_.attrs.end()) {
      player_.attrs["hp"] = 100;  // 没配就默认 100
    }
    player_.hp = player_.attrs["hp"];  // 运行时血量从属性表出发

  }
  // ★ 不再单独存"游戏类型" ★
  //   跟随相机 = 横版平台；固定/自动卷轴 = 飞行射击。这本来就是相机决定了玩法。
  const bool free2d = (s.camera_mode == "fixed" || s.camera_mode == "auto_scroll");
  control_ = free2d ? ControlProfile::kFree2d : ControlProfile::kPlatformer;
  if (s.camera_mode == "auto_scroll") {
    camera_profile_ = CameraProfile::kAutoScroll;
  } else if (s.camera_mode == "fixed") {
    camera_profile_ = CameraProfile::kFixed;
  } else {
    camera_profile_ = CameraProfile::kFollow;
  }
  lives_ = std::max(1, s.lives);
  game_over_ = false;
  shooter_time_ = 0.0f;
  shooter_fire_timer_ = 0.0f;
  shooter_dive_timer_ = 0.0f;
  shooter_bullets_.clear();
  shooter_enemies_.clear();
  InitSceneRuntime();
  UpdateCamera(0.016f);
  PlaySceneBgm();
}

void Game::InitSceneRuntime() {
  if (control_ == ControlProfile::kPlatformer) {
    InitEnemies();
  } else if (camera_profile_ == CameraProfile::kAutoScroll) {
    InitHShooter();
  } else {
    InitShooter();
  }
}

void Game::InitEnemies() {
  enemies_.clear();
  checkpoints_.clear();
  projectiles_.clear();
  if (p3d_ == nullptr) return;
  const Scene& s = p3d_->scenes[scene_index_];
  for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
    const SceneObject& so = s.objects[i];
    if (so.object < 0 || so.object >= static_cast<int>(p3d_->objects.size())) continue;
    if (so.object == player_object_) continue;
    const Object& o = p3d_->objects[so.object];
    if (o.type == "checkpoint") {
      CheckpointState cp;
      cp.object = so.object;
      cp.x = static_cast<float>(so.x);
      cp.y = static_cast<float>(so.y);
      checkpoints_.push_back(cp);
      continue;
    }
    if (o.type != "enemy") continue;
    EnemyState e;
    e.object = so.object;
    e.scene_object = i;
    e.x = static_cast<float>(so.x);
    e.y = static_cast<float>(so.y);
    e.home_x = e.x;
    for (const ObjectAttr& a : o.attrs) e.attrs[a.name] = a.value;
    if (e.attrs.find("hp") == e.attrs.end()) e.attrs["hp"] = 1;
    e.hp = std::max(1, e.attrs["hp"]);
    e.attack_timer = static_cast<float>(std::max(1, ObjIntAttr(o, "fire_interval", 120))) / 60.0f;
    e.dir = -1;
    e.facing = -1;
    e.action = o.default_action;
    if (e.action < 0 || e.action >= static_cast<int>(o.actions.size())) e.action = 0;
    for (int a = 0; a < static_cast<int>(o.actions.size()); ++a) {
      if (o.actions[a].name == "death") {
        e.death_action = a;
        break;
      }
    }
    enemies_.push_back(e);
  }
}

void Game::InitShooter() {
  shooter_enemies_.clear();
  shooter_bullets_.clear();
  shooter_explosions_.clear();
  shooter_bullet_object_ = -1;
  shooter_player_image_ = -1;
  shooter_effect_object_ = -1;
  if (p3d_ == nullptr) return;
  for (int i = 0; i < static_cast<int>(p3d_->objects.size()); ++i) {
    if (p3d_->objects[i].type == "player") {
      player_object_ = i;
      shooter_player_image_ = p3d_->objects[i].image;
      break;
    }
  }
  for (int i = 0; i < static_cast<int>(p3d_->objects.size()); ++i) {
    if (p3d_->objects[i].type == "bullet") {
      shooter_bullet_object_ = i;
      break;
    }
  }
  for (int i = 0; i < static_cast<int>(p3d_->objects.size()); ++i) {
    if (p3d_->objects[i].type == "effect") {
      shooter_effect_object_ = i;
      break;
    }
  }
  const Scene& s = p3d_->scenes[scene_index_];
  for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
    const SceneObject& so = s.objects[i];
    if (so.object < 0 || so.object >= static_cast<int>(p3d_->objects.size())) continue;
    if (so.object == player_object_) continue;
    const Object& o = p3d_->objects[so.object];
    if (o.type != "enemy") continue;
    ShooterEnemy e;
    e.object = so.object;
    e.scene_object = i;
    e.home_x = static_cast<float>(so.x);
    e.home_y = static_cast<float>(so.y);
    e.x = e.home_x;
    e.y = e.home_y;
    e.phase = static_cast<float>(shooter_enemies_.size()) * 0.73f;
    shooter_enemies_.push_back(e);
  }
  if (player_object_ >= 0) {
    const Object& po = p3d_->objects[player_object_];
    const float half_w = static_cast<float>(std::max(4, po.frame_w)) * 0.5f;
    const float half_h = static_cast<float>(std::max(4, po.frame_h)) * 0.5f;
    player_.x = std::max(half_w, std::min(view_w_ - half_w, player_.x));
    player_.y = std::max(half_h, std::min(view_h_ - half_h, player_.y));
  }
  shooter_fire_timer_ = 0.0f;
  shooter_dive_timer_ = 0.0f;
  shooter_time_ = 0.0f;
}

void Game::ShooterHitPlayer() {
  if (invuln_ > 0.0f || game_over_) return;
  --lives_;
  AudioPlay(Sfx::kHurt);
  if (player_object_ >= 0 &&
      player_object_ < static_cast<int>(p3d_->objects.size())) {
  }
  invuln_ = 1.5f;
  toast_ = "OUCH";
  toast_timer_ = 1.0f;
  if (lives_ <= 0) {
    lives_ = 0;
    game_over_ = true;
    toast_.clear();  // GAME OVER 由渲染层的横幅统一画
    toast_timer_ = 0.0f;
    AudioPlay(Sfx::kClear);
  } else if (player_object_ >= 0) {
    const Object& po = p3d_->objects[player_object_];
    player_.x = 200.0f;
    player_.y = 208.0f;
    (void)po;
  }
}

void Game::InitHShooter() {
  shooter_enemies_.clear();
  shooter_bullets_.clear();
  shooter_explosions_.clear();
  shooter_bullet_object_ = -1;
  shooter_player_image_ = -1;
  shooter_effect_object_ = -1;
  hscroll_x_ = 0.0f;
  vscroll_y_ = 0.0f;
  if (p3d_ == nullptr) return;
  for (int i = 0; i < static_cast<int>(p3d_->objects.size()); ++i) {
    if (p3d_->objects[i].type == "player") {
      player_object_ = i;
      shooter_player_image_ = p3d_->objects[i].image;
      break;
    }
  }
  for (int i = 0; i < static_cast<int>(p3d_->objects.size()); ++i) {
    if (p3d_->objects[i].type == "bullet") {
      shooter_bullet_object_ = i;
      break;
    }
  }
  for (int i = 0; i < static_cast<int>(p3d_->objects.size()); ++i) {
    if (p3d_->objects[i].type == "effect") {
      shooter_effect_object_ = i;
      break;
    }
  }
  const Scene& s = p3d_->scenes[scene_index_];
  for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
    const SceneObject& so = s.objects[i];
    if (so.object < 0 || so.object >= static_cast<int>(p3d_->objects.size())) continue;
    if (so.object == player_object_) continue;
    const Object& o = p3d_->objects[so.object];
    if (o.type != "enemy") continue;
    ShooterEnemy e;
    e.object = so.object;
    e.scene_object = i;
    e.home_x = static_cast<float>(so.x);
    e.home_y = static_cast<float>(so.y);
    e.x = e.home_x;
    e.y = e.home_y;
    e.phase = static_cast<float>(shooter_enemies_.size()) * 0.73f;
    shooter_enemies_.push_back(e);
  }
  shooter_fire_timer_ = 0.0f;
  shooter_time_ = 0.0f;
}

void Game::HShooterHitPlayer() {
  if (invuln_ > 0.0f || game_over_) return;
  --lives_;
  AudioPlay(Sfx::kHurt);
  invuln_ = 1.5f;
  toast_ = "OUCH";
  toast_timer_ = 1.0f;
  if (lives_ <= 0) {
    lives_ = 0;
    game_over_ = true;
    toast_.clear();
    toast_timer_ = 0.0f;
    AudioPlay(Sfx::kClear);
    return;
  }
  if (player_object_ >= 0) {
    const Object& po = p3d_->objects[player_object_];
    player_.x = hscroll_x_ + 80.0f;
    player_.y = 120.0f;
    (void)po;
  }
}

void Game::UpdateHShooter(float dt, const eng::InputState& input) {
  shooter_time_ += dt;
  if (p3d_ == nullptr || player_object_ < 0 || game_over_) return;
  if (invuln_ > 0.0f) invuln_ -= dt;
  if (toast_timer_ > 0.0f) toast_timer_ -= dt;
  const Scene& s = p3d_->scenes[scene_index_];
  hscroll_x_ += static_cast<float>(std::max(0, s.scroll_speed)) * dt;
  vscroll_y_ += static_cast<float>(s.scroll_speed_y) * dt;  // 正值 = 内容向下
  // 射击关卡通关分数线
  if (s.win_score > 0 && score_ >= s.win_score) {
    won_ = true;
    toast_.clear();
    toast_timer_ = 0.0f;
  }
  // 卷轴关卡结束：卷到指定距离就切场景
  if (!s.scroll_next.empty() &&
      ((s.scroll_end_x > 0 && hscroll_x_ >= static_cast<float>(s.scroll_end_x)) ||
       (s.scroll_end_y > 0 && vscroll_y_ >= static_cast<float>(s.scroll_end_y)))) {
    for (size_t si = 0; si < p3d_->scenes.size(); ++si) {
      if (p3d_->scenes[si].id == s.scroll_next) {
        const int hp = player_.hp;
        const int score = score_;
        ++transition_count_;
        SetScene(static_cast<int>(si));
        player_.hp = hp;
        score_ = score;
        return;
      }
    }
  }
  const Object& po = p3d_->objects[player_object_];
  const float half_w = static_cast<float>(std::max(4, po.frame_w)) * 0.5f;
  const float half_h = static_cast<float>(std::max(4, po.frame_h)) * 0.5f;
  const float speed = static_cast<float>(po.speed > 0 ? po.speed : 160);
  int dx = 0, dy = 0;
  if (eng::KeyDown(input, eng::Key::kLeft)) --dx;
  if (eng::KeyDown(input, eng::Key::kRight)) ++dx;
  if (eng::KeyDown(input, eng::Key::kUp)) --dy;
  if (eng::KeyDown(input, eng::Key::kDown)) ++dy;
  player_.x += static_cast<float>(dx) * speed * dt;
  player_.y += static_cast<float>(dy) * speed * dt;
  player_.x = std::max(hscroll_x_ + half_w,
                       std::min(hscroll_x_ + view_w_ - half_w, player_.x));
  player_.y = std::max(half_h, std::min(view_h_ - half_h, player_.y));
  if (dx != 0) player_.facing = dx > 0 ? 1 : -1;

  shooter_fire_timer_ -= dt;
  if (shooter_bullet_object_ >= 0 && shooter_fire_timer_ <= 0.0f &&
      eng::KeyDown(input, eng::Key::kA)) {
    const Object& bo = p3d_->objects[shooter_bullet_object_];
    ShooterBullet b;
    b.object = shooter_bullet_object_;
    b.x = player_.x + half_w;
    b.y = player_.y;
    b.vx = static_cast<float>(ObjIntAttr(bo, "bullet_speed", 300));
    b.from_player = true;
    shooter_bullets_.push_back(b);
    if (!po.spawners.empty()) SpawnProjectiles(po, player_.x, player_.y, 0.0f, 0.0f, true, -1);
    shooter_fire_timer_ = static_cast<float>(ObjIntAttr(po, "fire_interval", 12)) / 60.0f;
    AudioPlay(Sfx::kJump);
  }

  for (ShooterEnemy& e : shooter_enemies_) {
    if (!e.alive) continue;
    const Object& eo = p3d_->objects[e.object];
    const float espeed = static_cast<float>(eo.speed > 0 ? eo.speed : 40);
    e.x -= espeed * dt;
    e.y = e.home_y + std::sin(shooter_time_ * 1.6f + e.phase) * 10.0f;
    e.fire_timer -= dt;
    if (e.fire_timer <= 0.0f && e.x > hscroll_x_ - 16.0f &&
        e.x < hscroll_x_ + 416.0f) {
      if (shooter_bullet_object_ >= 0) {
        const Object& bo = p3d_->objects[shooter_bullet_object_];
        ShooterBullet b;
        b.object = shooter_bullet_object_;
        b.x = e.x - 8.0f;
        b.y = e.y;
        b.vx = -static_cast<float>(ObjIntAttr(bo, "bullet_speed", 300));
        b.from_player = false;
        shooter_bullets_.push_back(b);
        if (!eo.spawners.empty()) SpawnProjectiles(eo, e.x, e.y, 0.0f, 0.0f, false, e.scene_object);
      }
      e.fire_timer = static_cast<float>(ObjIntAttr(eo, "fire_interval", 120)) / 60.0f;
    }
    if (e.x < hscroll_x_ - 40.0f) e.alive = false;

    const float px0 = player_.x + static_cast<float>(po.col_x);
    const float px1 = px0 + static_cast<float>(po.col_w);
    const float py0 = player_.y + static_cast<float>(po.col_y);
    const float py1 = py0 + static_cast<float>(po.col_h);
    const float ex0 = e.x + static_cast<float>(eo.col_x);
    const float ex1 = ex0 + static_cast<float>(eo.col_w);
    const float ey0 = e.y + static_cast<float>(eo.col_y);
    const float ey1 = ey0 + static_cast<float>(eo.col_h);
    if (px1 > ex0 && px0 < ex1 && py1 > ey0 && py0 < ey1) {
      HShooterHitPlayer();
      e.alive = false;
    }
  }

  for (size_t bi = 0; bi < shooter_bullets_.size();) {
    ShooterBullet& b = shooter_bullets_[bi];
    b.x += b.vx * dt;
    b.y += b.vy * dt;
    bool remove = b.x < hscroll_x_ - 32.0f || b.x > hscroll_x_ + 432.0f ||
                  b.y < -32.0f || b.y > 272.0f;
    if (!remove && b.from_player) {
      for (ShooterEnemy& e : shooter_enemies_) {
        if (!e.alive) continue;
        const Object& eo = p3d_->objects[e.object];
        const float ex0 = e.x + static_cast<float>(eo.col_x);
        const float ex1 = ex0 + static_cast<float>(eo.col_w);
        const float ey0 = e.y + static_cast<float>(eo.col_y);
        const float ey1 = ey0 + static_cast<float>(eo.col_h);
        if (b.x > ex0 && b.x < ex1 && b.y > ey0 && b.y < ey1) {
          e.alive = false;
          score_ += ObjIntAttr(eo, "score", 100);
          if (shooter_effect_object_ >= 0) {
            ShooterExplosion fx;
            fx.object = shooter_effect_object_;
            fx.x = e.x;
            fx.y = e.y;
            shooter_explosions_.push_back(fx);
          }
          remove = true;
          AudioPlay(Sfx::kCoin);
          break;
        }
      }
    } else if (!remove && !b.from_player) {
      const float px0 = player_.x + static_cast<float>(po.col_x);
      const float px1 = px0 + static_cast<float>(po.col_w);
      const float py0 = player_.y + static_cast<float>(po.col_y);
      const float py1 = py0 + static_cast<float>(po.col_h);
      if (b.x > px0 && b.x < px1 && b.y > py0 && b.y < py1) {
        HShooterHitPlayer();
        remove = true;
      }
    }
    if (remove) {
      shooter_bullets_.erase(shooter_bullets_.begin() + bi);
    } else {
      ++bi;
    }
  }
  for (size_t ei = 0; ei < shooter_explosions_.size();) {
    shooter_explosions_[ei].time += dt;
    if (shooter_explosions_[ei].time > 0.45f) {
      shooter_explosions_.erase(shooter_explosions_.begin() + ei);
    } else {
      ++ei;
    }
  }
  UpdateAnimation(dt);
}

// 把一个字符串画在屏幕坐标（供 toast / 交互提示用）
void Game::DrawToast(eng::DrawList& dl, const eng::TextureId* textures,
                     int texture_count) {
  // ★ 调试叠加（按 Start 切换）：玩家坐标 / 当前交互 / 触发区矩形 ★
  if (debug_on_ && p3d_ != nullptr && !p3d_->scenes.empty()) {
    const Scene& sc = p3d_->scenes[scene_index_];
    char buf[128];
    std::snprintf(buf, sizeof(buf), "X %.0f Y %.0f ACT %d", player_.x, player_.y,
                  active_interaction_);
    UiDrawTextLine(dl, 4, 4, buf, 0xFF00FF00u, 0, 0);
    for (size_t i = 0; i < sc.triggers.size(); ++i) {
      const Trigger& t = sc.triggers[i];
      if (!t.interaction.enabled && t.mods.empty()) continue;
      const float x0 = static_cast<float>(t.x) - camera_x_;
      const float y0 = static_cast<float>(t.y) - camera_y_;
      dl.DrawRect(eng::Rect2{eng::Vec2{x0, y0},
                             eng::Vec2{static_cast<float>(t.w), static_cast<float>(t.h)}},
                  0xFF00FFFFu);
      std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i));
      UiDrawTextLine(dl, static_cast<int>(x0 + 2), static_cast<int>(y0 + 2), buf,
                 0xFF00FFFFu, 0, 0);
    }
  }
  DrawUiScreens(dl, textures, texture_count);
  if (toast_timer_ <= 0.0f || toast_.empty()) return;
  UiDrawTextLine(dl, 0, 12, toast_, 0xFFFFFFFFu, 1, view_w_);
}

int Game::UiValue(const char* name) const {
  if (name == nullptr || name[0] == '\0') return 0;
  if (std::strcmp(name, "score") == 0) return score_;
  if (std::strcmp(name, "lives") == 0) return lives_;
  auto it = variables_.find(name);
  if (it != variables_.end()) return it->second;
  auto at = player_.attrs.find(name);
  if (at != player_.attrs.end()) return at->second;
  return 0;
}

bool Game::SaveBytes(std::string* out) const {
  if (out == nullptr || p3d_ == nullptr || p3d_->scenes.empty()) return false;
  out->clear();
  out->append("PLSV", 4);
  PutU16(out, 1);  // version
  PutStr(out, p3d_->scenes[static_cast<size_t>(scene_index_)].id);
  PutI32(out, static_cast<int>(std::lround(player_.x)));
  PutI32(out, static_cast<int>(std::lround(player_.y)));
  std::vector<std::pair<std::string, int>> globals;
  for (const auto& kv : variables_) {
    if (kv.first.rfind("g:", 0) == 0) {
      globals.emplace_back(kv.first.substr(2), kv.second);
    }
  }
  PutU16(out, static_cast<unsigned>(globals.size()));
  for (const auto& kv : globals) {
    PutStr(out, kv.first);
    PutI32(out, kv.second);
  }
  return true;
}

bool Game::SaveExists() const {
  std::error_code ec;
  return std::filesystem::exists(save_path_, ec);
}

void Game::StartNewGame() {
  if (p3d_ == nullptr) return;
  variables_.clear();
  globals_seeded_ = false;
  score_ = 0;
  CloseUi();
  SetScene(p3d_->initial_scene);
}

bool Game::LoadBytes(const std::string& data) {
  if (p3d_ == nullptr || data.size() < 6) return false;
  if (data.compare(0, 4, "PLSV") != 0) return false;
  size_t pos = 4;
  unsigned version = 0;
  if (!GetU16(data, &pos, &version) || version != 1) return false;
  std::string scene_id;
  int px = 0;
  int py = 0;
  if (!GetStr(data, &pos, &scene_id) || !GetI32(data, &pos, &px) ||
      !GetI32(data, &pos, &py)) {
    return false;
  }
  unsigned count = 0;
  if (!GetU16(data, &pos, &count) || count > 4096) return false;
  std::vector<std::pair<std::string, int>> globals;
  for (unsigned i = 0; i < count; ++i) {
    std::string key;
    int value = 0;
    if (!GetStr(data, &pos, &key) || !GetI32(data, &pos, &value)) return false;
    globals.emplace_back(std::move(key), value);
  }
  int scene_index = -1;
  for (int i = 0; i < static_cast<int>(p3d_->scenes.size()); ++i) {
    if (p3d_->scenes[static_cast<size_t>(i)].id == scene_id) {
      scene_index = i;
      break;
    }
  }
  if (scene_index < 0) return false;
  SetScene(scene_index);
  for (auto it = variables_.begin(); it != variables_.end();) {
    if (it->first.rfind("g:", 0) == 0) it = variables_.erase(it);
    else ++it;
  }
  for (const auto& kv : globals) variables_["g:" + kv.first] = kv.second;
  globals_seeded_ = true;
  player_.x = static_cast<float>(px);
  player_.y = static_cast<float>(py);
  won_ = false;
  game_over_ = false;
  toast_.clear();
  toast_timer_ = 0.0f;
  UpdateCamera(0.0f);
  return true;
}

bool Game::SaveToFile(const std::string& path) const {
  std::string data;
  if (!SaveBytes(&data)) return false;
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return f.good();
}

bool Game::LoadFromFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string data((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  return LoadBytes(data);
}


void Game::DrawUiScreens(eng::DrawList& dl, const eng::TextureId* textures,
                         int texture_count) const {
  if (p3d_ == nullptr || p3d_->scenes.empty()) return;
  UiDrawContext ctx;
  ctx.save_exists = SaveExists();
  ctx.dl = &dl;
  ctx.view_w = view_w_;
  ctx.view_h = view_h_;
  ctx.value = [](void* user, const char* name) {
    return static_cast<const Game*>(user)->UiValue(name);
  };
  ctx.user = const_cast<Game*>(this);
  const Scene& sc = p3d_->scenes[scene_index_];
  if (sc.hud_ui >= 0 && sc.hud_ui < static_cast<int>(p3d_->ui_screens.size())) {
    const UiScreen screen =
        UiScreenFromData(p3d_->ui_screens[static_cast<size_t>(sc.hud_ui)],
                         textures, texture_count);
    UiDrawScreen(screen, ctx);
  }
  if (p3d_->persistent_ui >= 0 &&
      p3d_->persistent_ui < static_cast<int>(p3d_->ui_screens.size())) {
    const UiScreen screen =
        UiScreenFromData(p3d_->ui_screens[static_cast<size_t>(p3d_->persistent_ui)],
                         textures, texture_count);
    UiDrawScreen(screen, ctx);
  }
  if (!modal_stack_.empty()) {
    dl.DrawRect(eng::Rect2{eng::Vec2{0.0f, 0.0f},
                           eng::Vec2{static_cast<float>(view_w_),
                                     static_cast<float>(view_h_)}},
                0x80000000u);
    for (size_t mi = 0; mi < modal_stack_.size(); ++mi) {
      const int idx = modal_stack_[mi];
      if (idx < 0 || idx >= static_cast<int>(p3d_->ui_screens.size())) continue;
      const UiScreen screen =
          UiScreenFromData(p3d_->ui_screens[static_cast<size_t>(idx)], textures,
                           texture_count);
      const bool top = (mi + 1 == modal_stack_.size());
      ctx.focus = top ? modal_focus_ : -1;
      ctx.list_selected = top ? modal_list_index_ : 0;
      UiDrawScreen(screen, ctx);
    }
    int fade_alpha = 0;
    if (modal_closing_ && modal_close_total_ > 0) {
      fade_alpha = modal_close_fade_ * 255 / modal_close_total_;
    } else if (modal_fade_ > 0 && modal_fade_total_ > 0) {
      fade_alpha = modal_fade_ * 255 / modal_fade_total_;
    }
    if (fade_alpha > 0) {
      fade_alpha = std::max(0, std::min(255, fade_alpha));
      dl.DrawRect(eng::Rect2{eng::Vec2{0.0f, 0.0f},
                             eng::Vec2{static_cast<float>(view_w_),
                                       static_cast<float>(view_h_)}},
                  static_cast<eng::u32>(fade_alpha) << 24);
    }
  }
}

bool Game::UiModalActive() const {
  if (p3d_ == nullptr || modal_stack_.empty()) return false;
  const int idx = modal_stack_.back();
  return idx >= 0 && idx < static_cast<int>(p3d_->ui_screens.size()) &&
         p3d_->ui_screens[static_cast<size_t>(idx)].modal;
}

void Game::OpenUiById(const std::string& id) {
  if (p3d_ == nullptr || id.empty()) return;
  for (int i = 0; i < static_cast<int>(p3d_->ui_screens.size()); ++i) {
    if (p3d_->ui_screens[static_cast<size_t>(i)].id == id) {
      if (!modal_stack_.empty() && modal_stack_.back() == i) return;
      modal_stack_.push_back(i);
      modal_focus_ = -1;
      const UiScreenData& screen = p3d_->ui_screens[static_cast<size_t>(i)];
      modal_list_index_ = 0;
      for (int k = 0; k < static_cast<int>(screen.elements.size()); ++k) {
        const UiElementData& el = screen.elements[static_cast<size_t>(k)];
        if (el.only_if_save && !SaveExists()) continue;
        if (el.kind == 7 || el.kind == 6) {
          modal_focus_ = k;
          break;
        }
      }
      modal_timer_ = screen.dismiss_frames;
      modal_fade_total_ = screen.fade_frames;
      modal_fade_ = modal_fade_total_;
      modal_closing_ = false;
      modal_close_fade_ = 0;
      modal_close_total_ = 0;
      return;
    }
  }
}

void Game::RequestCloseUi() {
  if (p3d_ == nullptr || modal_stack_.empty() || modal_closing_) return;
  const int idx = modal_stack_.back();
  if (idx < 0 || idx >= static_cast<int>(p3d_->ui_screens.size())) {
    CloseUi();
    return;
  }
  const int total = p3d_->ui_screens[static_cast<size_t>(idx)].fade_out_frames;
  if (total <= 0) {
    CloseUi();
    return;
  }
  modal_closing_ = true;
  modal_close_fade_ = 0;
  modal_close_total_ = total;
}

void Game::CloseUi() {
  if (!modal_stack_.empty()) modal_stack_.pop_back();
  modal_focus_ = -1;
  modal_list_index_ = 0;
  modal_timer_ = 0;
  modal_fade_ = 0;
  modal_fade_total_ = 0;
  modal_closing_ = false;
  modal_close_fade_ = 0;
  modal_close_total_ = 0;
  if (modal_stack_.empty() && has_pending_ui_) {
    has_pending_ui_ = false;
    const Trigger trigger = pending_trigger_;
    std::vector<TriggerAction> actions = std::move(pending_actions_);
    pending_actions_.clear();
    RunTriggerList(trigger, actions);
  }
}

void Game::MoveModalFocus(int dir, bool allow_list) {
  if (p3d_ == nullptr || modal_stack_.empty()) return;
  const int idx = modal_stack_.back();
  if (idx < 0 || idx >= static_cast<int>(p3d_->ui_screens.size())) return;
  const UiScreenData& screen = p3d_->ui_screens[static_cast<size_t>(idx)];
  if (allow_list && modal_focus_ >= 0 &&
      modal_focus_ < static_cast<int>(screen.elements.size())) {
    const UiElementData& e = screen.elements[static_cast<size_t>(modal_focus_)];
    if (e.kind == 6 && !e.items.empty()) {
      modal_list_index_ = std::max(
          0, std::min(static_cast<int>(e.items.size()) - 1,
                      modal_list_index_ + dir));
      return;
    }
  }
  std::vector<int> focusables;
  for (int i = 0; i < static_cast<int>(screen.elements.size()); ++i) {
    const UiElementData& el = screen.elements[static_cast<size_t>(i)];
    if (el.only_if_save && !SaveExists()) continue;
    if (el.kind == 7 || el.kind == 6) focusables.push_back(i);
  }
  if (focusables.empty()) return;
  int pos = 0;
  for (int i = 0; i < static_cast<int>(focusables.size()); ++i) {
    if (focusables[static_cast<size_t>(i)] == modal_focus_) pos = i;
  }
  pos = (pos + dir + static_cast<int>(focusables.size())) %
        static_cast<int>(focusables.size());
  modal_focus_ = focusables[static_cast<size_t>(pos)];
  modal_list_index_ = 0;
}

void Game::ActivateModalFocus() {
  if (p3d_ == nullptr || modal_stack_.empty()) return;
  const int idx = modal_stack_.back();
  if (idx < 0 || idx >= static_cast<int>(p3d_->ui_screens.size())) return;
  const UiScreenData& screen = p3d_->ui_screens[static_cast<size_t>(idx)];
  if (modal_focus_ < 0 ||
      modal_focus_ >= static_cast<int>(screen.elements.size())) {
    return;
  }
  const UiElementData& e = screen.elements[static_cast<size_t>(modal_focus_)];
  if (e.kind == 6) {
    if (e.items.empty()) return;
    const int sel = std::max(
        0, std::min(static_cast<int>(e.items.size()) - 1, modal_list_index_));
    const std::string& item = e.items[static_cast<size_t>(sel)];
    if (e.action.empty() || e.action == "close") {
      RequestCloseUi();
      return;
    }
    if (e.action == "scene") {
      for (int i = 0; i < static_cast<int>(p3d_->scenes.size()); ++i) {
        if (p3d_->scenes[static_cast<size_t>(i)].id == item) {
          SetScene(i);
          return;
        }
      }
      return;
    }
    if (e.action == "open_ui") {
      OpenUiById(item);
      return;
    }
    if (e.action == "set_var") {
      variables_[e.action_target] = sel;
      return;
    }
    return;
  }
  if (e.kind != 7) return;
  if (e.action == "new_game") {
    StartNewGame();
    return;
  }
  if (e.action == "load") {
    if (LoadFromFile(save_path_)) CloseUi();
    return;
  }
  if (e.action.empty() || e.action == "close") {
    RequestCloseUi();
    return;
  }
  if (e.action == "open_ui") {
    OpenUiById(e.action_target);
    return;
  }
  if (e.action == "set_var") {
    variables_[e.action_target] = e.action_value;
    return;
  }
  if (e.action == "scene") {
    for (int i = 0; i < static_cast<int>(p3d_->scenes.size()); ++i) {
      if (p3d_->scenes[static_cast<size_t>(i)].id == e.action_target) {
        SetScene(i);
        return;
      }
    }
  }
}

void Game::RenderHShooter(eng::DrawList& dl, const eng::TextureId* textures,
                          int texture_count) const {
  dl.Clear(0xFF000000u);
  for (int i = 0; i < 120; ++i) {
    const int x = (i * 97 + static_cast<int>(hscroll_x_ * 0.4f)) % 400;
    const int y = ((i * 53) + static_cast<int>(vscroll_y_ * 0.5f)) % 240;
    const bool bright = (i % 5) == 0;
    dl.DrawRect(eng::Rect2{eng::Vec2{static_cast<float>(x), static_cast<float>(y)},
                           eng::Vec2{bright ? 2.0f : 1.0f, bright ? 2.0f : 1.0f}},
                bright ? 0xFFE8F2FFu : 0xFF8FA4C0u);
  }
  auto draw_obj = [&](int object, float x, float y, int frame, eng::u32 tint) {
    if (object < 0 || object >= static_cast<int>(p3d_->objects.size())) return;
    const Object& o = p3d_->objects[object];
    if (o.image < 0 || o.image >= texture_count) return;
    const eng::TextureId tex = textures[o.image];
    if (tex == 0) return;
    int frame_no = 0;
    int origin_x = 0;
    int origin_y = 0;
    const Action* act = nullptr;
    int action = o.default_action;
    const int fw0 = std::max(1, o.frame_w);
    const int fh0 = std::max(1, o.frame_h);
    origin_x = fw0 / 2;
    origin_y = fh0 / 2;
    if (action >= 0 && action < static_cast<int>(o.actions.size())) {
      act = &o.actions[action];
      if (!act->frames.empty()) {
        int fi = frame;
        if (fi < 0) fi = 0;
        if (fi >= static_cast<int>(act->frames.size())) fi = static_cast<int>(act->frames.size()) - 1;
        frame_no = act->frames[fi].image;
        origin_x = act->frames[fi].origin_x;
        origin_y = act->frames[fi].origin_y;
      }
    }
    const int fw = fw0;
    const int fh = fh0;
    const int off_x = act != nullptr ? act->offset_x : 0;
    const int off_y = act != nullptr ? act->offset_y : 0;
    const int tex_w = o.image < static_cast<int>(p3d_->images.size())
                          ? p3d_->images[o.image].width
                          : fw;
    const int tc = (tex_w / fw > 0) ? frame_no % (tex_w / fw) : 0;
    const int tr = (tex_w / fw > 0) ? frame_no / (tex_w / fw) : 0;
    eng::u8 flags = 0;
    if (act != nullptr && act->flip_h) flags = static_cast<eng::u8>(flags | eng::kDrawFlipH);
    if (act != nullptr && act->flip_v) flags = static_cast<eng::u8>(flags | eng::kDrawFlipV);
    dl.DrawImage(eng::Rect2{eng::Vec2{x - static_cast<float>(origin_x) + static_cast<float>(off_x),
                                      y - static_cast<float>(origin_y) + static_cast<float>(off_y)},
                            eng::Vec2{static_cast<float>(fw), static_cast<float>(fh)}},
                 tex, tint, eng::TexRegion::Make(tc * fw, tr * fh, fw, fh), flags);
  };
  for (const ShooterEnemy& e : shooter_enemies_) {
    if (!e.alive) continue;
    draw_obj(e.object, e.x - hscroll_x_, e.y - vscroll_y_, 0, 0xFFFFFFFFu);
  }
  if (shooter_player_image_ >= 0 && shooter_player_image_ < texture_count &&
      textures[shooter_player_image_] != 0 && player_object_ >= 0) {
    const Object& po = p3d_->objects[player_object_];
    const int fw = std::max(1, po.frame_w);
    const int fh = std::max(1, po.frame_h);
    const eng::u32 tint = (invuln_ > 0.0f && (frame_count_ / 4) % 2 == 0)
                             ? 0x70FFFFFFu : 0xFFFFFFFFu;
    dl.DrawImage(eng::Rect2{eng::Vec2{player_.x - hscroll_x_ - fw * 0.5f,
                                      player_.y - vscroll_y_ - fh * 0.5f},
                            eng::Vec2{static_cast<float>(fw), static_cast<float>(fh)}},
                 textures[shooter_player_image_], tint,
                 eng::TexRegion::Make(0, 0, fw, fh));
  }
  for (const ShooterExplosion& fx : shooter_explosions_) {
    const int frame = std::min(2, static_cast<int>(fx.time * 12.0f));
    draw_obj(fx.object, fx.x - hscroll_x_, fx.y, frame, 0xFFFFFFFFu);
  }
  for (const ShooterBullet& b : shooter_bullets_) {
    if (b.object >= 0) {
      draw_obj(b.object, b.x - hscroll_x_, b.y - vscroll_y_, 0,
               b.from_player ? 0xFFFFFFFFu : 0xFF80A0FFu);
    }
  }
  if (game_over_) {
    UiDrawTextLine(dl, 0, 150, "GAME OVER", 0xFFFF5050u, 1, view_w_);
  }
}

void Game::UpdateShooter(float dt, const eng::InputState& input) {
  shooter_time_ += dt;
  if (game_over_) return;
  if (invuln_ > 0.0f) invuln_ -= dt;
  if (toast_timer_ > 0.0f) toast_timer_ -= dt;
  if (p3d_ == nullptr || player_object_ < 0) return;
  const Object& po = p3d_->objects[player_object_];
  const float half_w = static_cast<float>(std::max(4, po.frame_w)) * 0.5f;
  const float half_h = static_cast<float>(std::max(4, po.frame_h)) * 0.5f;
  const float speed = static_cast<float>(po.speed > 0 ? po.speed : 160);

  int dx = 0, dy = 0;
  if (eng::KeyDown(input, eng::Key::kLeft)) --dx;
  if (eng::KeyDown(input, eng::Key::kRight)) ++dx;
  if (eng::KeyDown(input, eng::Key::kUp)) --dy;
  if (eng::KeyDown(input, eng::Key::kDown)) ++dy;
  // 固定镜头/俯视关也走图块碰撞层，不再只是夹屏幕。
  // 运行时需要推动场景实例，p3d_ 声明为 const 只限制别处；这里明确改实例位置。
  Scene& sc = const_cast<Scene&>(p3d_->scenes[scene_index_]);
  auto object_aabb = [&](int si, float* x0, float* y0, float* x1, float* y1) {
    const SceneObject& so = sc.objects[static_cast<size_t>(si)];
    if (so.object < 0 || so.object >= static_cast<int>(p3d_->objects.size())) return false;
    const Object& oo = p3d_->objects[static_cast<size_t>(so.object)];
    const AttackBox ob = ColBoxOf(oo, oo.default_action, 0);
    if (ob.w <= 0 || ob.h <= 0) return false;
    *x0 = static_cast<float>(so.x) + static_cast<float>(ob.x);
    *y0 = static_cast<float>(so.y) + static_cast<float>(ob.y);
    *x1 = *x0 + static_cast<float>(ob.w);
    *y1 = *y0 + static_cast<float>(ob.h);
    return true;
  };
  auto overlap_index = [&]() {
    const AttackBox pb = ColBoxOf(po, player_.action, player_.frame);
    const float px0 = player_.x + static_cast<float>(pb.x);
    const float px1 = px0 + static_cast<float>(pb.w);
    const float py0 = player_.y + static_cast<float>(pb.y);
    const float py1 = py0 + static_cast<float>(pb.h);
    for (size_t si = 0; si < sc.objects.size(); ++si) {
      if (static_cast<int>(si) == sc.controlled) continue;
      float ox0, oy0, ox1, oy1;
      if (!object_aabb(static_cast<int>(si), &ox0, &oy0, &ox1, &oy1)) continue;
      if (px1 > ox0 && px0 < ox1 && py1 > oy0 && py0 < oy1) return static_cast<int>(si);
    }
    return -1;
  };
  auto box_blocked = [&](int self, float x0, float y0, float x1, float y1) {
    const int c0 = static_cast<int>(std::floor(x0 / 8.0f));
    const int c1 = static_cast<int>(std::floor((x1 - 0.001f) / 8.0f));
    const int r0 = static_cast<int>(std::floor(y0 / 8.0f));
    const int r1 = static_cast<int>(std::floor((y1 - 0.001f) / 8.0f));
    for (int r = r0; r <= r1; ++r)
      for (int c = c0; c <= c1; ++c)
        if (TileWallsAt(c, r) != 0) return true;
    for (size_t si = 0; si < sc.objects.size(); ++si) {
      if (static_cast<int>(si) == self || static_cast<int>(si) == sc.controlled) continue;
      float ox0, oy0, ox1, oy1;
      if (!object_aabb(static_cast<int>(si), &ox0, &oy0, &ox1, &oy1)) continue;
      if (x1 > ox0 && x0 < ox1 && y1 > oy0 && y0 < oy1) return true;
    }
    return false;
  };
  auto try_push = [&](float delta, int axis) {
    const int hit = overlap_index();
    if (hit < 0) return true;
    SceneObject& so = sc.objects[static_cast<size_t>(hit)];
    const Object& oo = p3d_->objects[static_cast<size_t>(so.object)];
    if (!oo.pushable) return false;
    const int step = static_cast<int>(std::lround(delta));
    if (step == 0) return false;
    const int old_x = so.x;
    const int old_y = so.y;
    if (axis == 0) so.x += step; else so.y += step;
    float x0, y0, x1, y1;
    const bool ok = object_aabb(hit, &x0, &y0, &x1, &y1) &&
                    !box_blocked(hit, x0, y0, x1, y1);
    if (!ok) {
      so.x = old_x;
      so.y = old_y;
      return false;
    }
    // 「可推动」条件规则的动作在推动成功这一帧执行（音效/动画等）。
    for (const Rule& r : oo.rules) {
      if (!r.enabled) continue;
      bool pushed_rule = false;
      for (const RuleCond& c : r.when) {
        if (c.type == 12) pushed_rule = true;
      }
      if (!pushed_rule) continue;
      for (const RuleEffect& e : r.effects) {
        if (e.type == 5) PlayClipById(e.text);
        if (e.type == 2) {
          for (size_t ai = 0; ai < oo.actions.size(); ++ai) {
            if (oo.actions[ai].name == e.text) {
              so.action = static_cast<int>(ai);
              so.anim_start = frame_count_;
              break;
            }
          }
        }
      }
    }
    return true;
  };
  const float old_x = player_.x;
  const float old_y = player_.y;
  player_.vx = static_cast<float>(dx) * speed;
  player_.vy = static_cast<float>(dy) * speed;
  const float move_x = player_.vx * dt;
  const float move_y = player_.vy * dt;
  ResolveX(move_x, player_object_);
  if (!try_push(move_x, 0)) player_.x = old_x;
  ResolveY(move_y, player_object_);
  if (!try_push(move_y, 1)) player_.y = old_y;
  player_.vx = 0.0f;
  player_.vy = 0.0f;
  player_.x = std::max(half_w, std::min(view_w_ - half_w, player_.x));
  player_.y = std::max(half_h, std::min(view_h_ - half_h, player_.y));
  if (dx != 0) player_.facing = dx > 0 ? 1 : -1;

  shooter_fire_timer_ -= dt;
  const int fire_ticks = ObjIntAttr(po, "fire_interval", 12);
  if (shooter_bullet_object_ >= 0 && shooter_fire_timer_ <= 0.0f &&
      eng::KeyDown(input, eng::Key::kA)) {
    const Object& bo = p3d_->objects[shooter_bullet_object_];
    ShooterBullet b;
    b.object = shooter_bullet_object_;
    b.x = player_.x;
    b.y = player_.y - half_h;
    // 帧上有 muzzle 连接点就从枪口出弹
    float mx = 0.0f;
    float my = 0.0f;
    if (PlayerAttachPoint("muzzle", &mx, &my)) {
      b.x = mx;
      b.y = my;
    }
    b.vy = -static_cast<float>(ObjIntAttr(bo, "bullet_speed", 300));
    b.from_player = true;
    shooter_bullets_.push_back(b);
    if (!po.spawners.empty()) SpawnProjectiles(po, player_.x, player_.y, 0.0f, 0.0f, true, -1);
    shooter_fire_timer_ = static_cast<float>(fire_ticks) / 60.0f;
    AudioPlay(Sfx::kJump);
  }

  for (ShooterEnemy& e : shooter_enemies_) {
    if (!e.alive) continue;
    const Object& eo = p3d_->objects[e.object];
    if (e.diving) {
      e.dive_t += dt;
      const float sx = player_.x - e.x;
      const float sy = player_.y - e.y;
      const float len = std::sqrt(sx * sx + sy * sy);
      if (len > 1.0f) {
        e.x += sx / len * 120.0f * dt;
        e.y += sy / len * 120.0f * dt;
      }
      if (e.dive_t > 2.5f || e.y > 250.0f) {
        e.diving = false;
        e.x = e.home_x;
        e.y = e.home_y;
        e.dive_wait = 0.0f;
      }
    } else {
      // 编队摆动 + 定期俯冲（以前是按对象配 behavior/patrol_range/dive_interval，
      // 三个字段只服务这一处，就固定成同一种节奏了）
      e.x = e.home_x + std::sin(shooter_time_ * 1.4f + e.phase) * 10.0f;
      e.y = e.home_y + std::sin(shooter_time_ * 0.9f + e.phase) * 3.0f;
      e.dive_wait += dt;
      if (e.dive_wait >= 4.0f) {
        e.diving = true;
        e.dive_t = 0.0f;
        e.dive_wait = 0.0f;
      }
    }

    const float ex0 = e.x + static_cast<float>(eo.col_x);
    const float ex1 = ex0 + static_cast<float>(eo.col_w);
    const float ey0 = e.y + static_cast<float>(eo.col_y);
    const float ey1 = ey0 + static_cast<float>(eo.col_h);
    const float px0 = player_.x + static_cast<float>(po.col_x);
    const float px1 = px0 + static_cast<float>(po.col_w);
    const float py0 = player_.y + static_cast<float>(po.col_y);
    const float py1 = py0 + static_cast<float>(po.col_h);
    if (px1 > ex0 && px0 < ex1 && py1 > ey0 && py0 < ey1) {
      ShooterHitPlayer();
      if (game_over_) return;
    }

    if (shooter_bullet_object_ >= 0) {
      e.fire_timer += dt;
      const float interval = static_cast<float>(std::max(1, ObjIntAttr(eo, "fire_interval", 120))) / 60.0f;
      if (e.fire_timer >= interval) {
        e.fire_timer = 0.0f;
        const Object& bo = p3d_->objects[shooter_bullet_object_];
        ShooterBullet b;
        b.object = shooter_bullet_object_;
        b.x = e.x;
        b.y = e.y + static_cast<float>(eo.frame_h) * 0.5f;
        b.vy = static_cast<float>(ObjIntAttr(bo, "bullet_speed", 300)) * 0.7f;
        b.from_player = false;
        shooter_bullets_.push_back(b);
        if (!eo.spawners.empty()) SpawnProjectiles(eo, e.x, e.y, 0.0f, 0.0f, false, e.scene_object);
      }
    }
  }

  for (size_t ei = 0; ei < shooter_explosions_.size();) {
    shooter_explosions_[ei].time += dt;
    if (shooter_explosions_[ei].time > 0.45f) {
      shooter_explosions_.erase(shooter_explosions_.begin() + ei);
    } else {
      ++ei;
    }
  }

  const int player_bullet_object = shooter_bullet_object_;
  for (size_t bi = 0; bi < shooter_bullets_.size();) {
    ShooterBullet& b = shooter_bullets_[bi];
    b.y += b.vy * dt;
    if (b.y < -16.0f || b.y > 256.0f) {
      shooter_bullets_.erase(shooter_bullets_.begin() + bi);
      continue;
    }
    bool remove = false;
    if (b.object < 0 || b.object >= static_cast<int>(p3d_->objects.size())) {
      shooter_bullets_.erase(shooter_bullets_.begin() + bi);
      continue;
    }
    const Object& bo = p3d_->objects[b.object];
    const float bx0 = b.x + static_cast<float>(bo.col_x);
    const float bx1 = bx0 + static_cast<float>(bo.col_w);
    const float by0 = b.y + static_cast<float>(bo.col_y);
    const float by1 = by0 + static_cast<float>(bo.col_h);
    if (b.from_player) {
      for (ShooterEnemy& e : shooter_enemies_) {
        if (!e.alive) continue;
        const Object& eo = p3d_->objects[e.object];
        const float ex0 = e.x + static_cast<float>(eo.col_x);
        const float ex1 = ex0 + static_cast<float>(eo.col_w);
        const float ey0 = e.y + static_cast<float>(eo.col_y);
        const float ey1 = ey0 + static_cast<float>(eo.col_h);
        if (bx1 > ex0 && bx0 < ex1 && by1 > ey0 && by0 < ey1) {
          e.alive = false;
          score_ += ObjIntAttr(eo, "score", 100);
          if (shooter_effect_object_ >= 0) {
            ShooterExplosion fx;
            fx.object = shooter_effect_object_;
            fx.x = e.x;
            fx.y = e.y;
            shooter_explosions_.push_back(fx);
          }
          remove = true;
          AudioPlay(Sfx::kCoin);
          break;
        }
      }
    } else {
      const float px0 = player_.x + static_cast<float>(po.col_x);
      const float px1 = px0 + static_cast<float>(po.col_w);
      const float py0 = player_.y + static_cast<float>(po.col_y);
      const float py1 = py0 + static_cast<float>(po.col_h);
      if (bx1 > px0 && bx0 < px1 && by1 > py0 && by0 < py1) {
        ShooterHitPlayer();
        remove = true;
      }
    }
    if (remove) {
      shooter_bullets_.erase(shooter_bullets_.begin() + bi);
    } else {
      ++bi;
    }
  }
  (void)player_bullet_object;

  bool any_alive = false;
  for (const ShooterEnemy& e : shooter_enemies_) {
    if (e.alive) { any_alive = true; break; }
  }
  if (!any_alive && !shooter_enemies_.empty() && !game_over_) {
    ShooterEnemy* none = nullptr;
    (void)none;
    for (ShooterEnemy& e : shooter_enemies_) {
      e.alive = true;
      e.x = e.home_x;
      e.y = e.home_y;
      e.fire_timer = 0.0f;
      e.diving = false;
    }
  }
}

void Game::UpdateEnemies(float dt) {
  if (p3d_ == nullptr || enemies_.empty()) return;
  const Object& po = p3d_->objects[player_object_];
  const float px0 = player_.x + static_cast<float>(po.col_x);
  const float px1 = px0 + static_cast<float>(po.col_w);
  const float py0 = player_.y + static_cast<float>(po.col_y);
  const float py1 = py0 + static_cast<float>(po.col_h);
  const float pbottom = py1;

  for (EnemyState& e : enemies_) {
    if (e.dead) continue;
    if (e.object < 0 || e.object >= static_cast<int>(p3d_->objects.size())) continue;
    const Object& eo = p3d_->objects[e.object];
    if (e.hit_invuln > 0.0f) e.hit_invuln -= dt;
    if (e.hit_flash > 0.0f) e.hit_flash -= dt;
    if (!e.dying) {
      if (e.attack_hit_cd > 0.0f) e.attack_hit_cd -= dt;
      e.attack_timer -= dt;
      if (!e.attacking && e.attack_timer <= 0.0f) {
        e.attacking = true;
        e.attack_hold = 0.35f;
        e.attack_timer = static_cast<float>(std::max(1, ObjIntAttr(eo, "fire_interval", 120))) / 60.0f;
        const int ai = FindActionIndex(eo, "attack");
        if (ai >= 0) {
          e.action = ai;
          e.anim_time = 0.0f;
          e.frame = 0;
        }
        SpawnProjectiles(eo, e.x, e.y,
                         e.dir * (eo.speed > 0 ? static_cast<float>(eo.speed) * 0.45f : 0.0f),
                         0.0f, false, e.scene_object);
      }
      if (e.attacking) {
        e.attack_hold -= dt;
        if (e.attack_hold <= 0.0f) {
          e.attacking = false;
          e.action = eo.default_action;
          e.anim_time = 0.0f;
          e.frame = 0;
        }
      }
    }

    if (e.dying) {
      if (e.death_action < 0) {
        e.dead = true;
        continue;
      }
      e.anim_time += dt;
      int action = e.death_action >= 0 ? e.death_action : e.action;
      if (action >= 0 && action < static_cast<int>(eo.actions.size())) {
        const Action& a = eo.actions[action];
        if (!a.frames.empty()) {
          bool ended = false;
          e.frame = ActionFrameAt(a, e.anim_time, &ended);
          if (ended) e.dead = true;
        } else {
          e.dead = true;
        }
      } else {
        e.dead = true;
      }
      continue;
    }

    e.tick++;
    if (!eo.rules.empty()) {
      RunEnemyRules(eo, e, dt);
    } else {
      const float speed = eo.speed > 0 ? static_cast<float>(eo.speed) * 0.45f : 30.0f;
      const float range = 64.0f;
      e.x += static_cast<float>(e.dir) * speed * dt;
      if (e.x < e.home_x - range) e.dir = 1;
      if (e.x > e.home_x + range) e.dir = -1;
      e.facing = e.dir;
    }
    e.anim_time += dt;
    if (e.action >= 0 && e.action < static_cast<int>(eo.actions.size())) {
      const Action& a = eo.actions[e.action];
      if (!a.frames.empty()) {
        e.frame = ActionFrameAt(a, e.anim_time, nullptr);
      }
    }

    if (invuln_ > 0.0f) continue;
    const float ex0 = e.x + static_cast<float>(eo.col_x);
    const float ex1 = ex0 + static_cast<float>(eo.col_w);
    const float ey0 = e.y + static_cast<float>(eo.col_y);
    const float ey1 = ey0 + static_cast<float>(eo.col_h);

    if (e.attacking && e.attack_hit_cd <= 0.0f &&
        ObjectTeam(eo) != ObjectTeam(po) && ObjIntAttr(eo, "atk", 0) > 0) {
      AttackBox phb{po.hurt_x, po.hurt_y, po.hurt_w, po.hurt_h};
      if (phb.w <= 0 || phb.h <= 0) {
        phb = ColBoxOf(po, player_.action, player_.frame);
      }
      if (player_.action >= 0 && player_.action < static_cast<int>(po.actions.size())) {
        const Action& pa = po.actions[player_.action];
        if (player_.frame >= 0 && player_.frame < static_cast<int>(pa.frames.size())) {
          const AttackBox& fb = pa.frames[player_.frame].hurt;
          if (fb.w > 0 && fb.h > 0) phb = AttackBox{fb.x, fb.y, fb.w, fb.h};
        }
      }
      AttackBox attack{eo.attack_x, eo.attack_y, eo.attack_w, eo.attack_h};
      if (e.action >= 0 && e.action < static_cast<int>(eo.actions.size())) {
        const Action& ea = eo.actions[e.action];
        if (e.frame >= 0 && e.frame < static_cast<int>(ea.frames.size())) {
          const AttackBox& fb = ea.frames[e.frame].attack;
          if (fb.w > 0 && fb.h > 0) attack = fb;
        }
      }
      const float hx0 = player_.x + static_cast<float>(phb.x);
      const float hy0 = player_.y + static_cast<float>(phb.y);
      const float hx1 = hx0 + static_cast<float>(phb.w);
      const float hy1 = hy0 + static_cast<float>(phb.h);
      const float ax0 = e.x + static_cast<float>(attack.x);
      const float ay0 = e.y + static_cast<float>(attack.y);
      const float ax1 = ax0 + static_cast<float>(attack.w);
      const float ay1 = ay0 + static_cast<float>(attack.h);
      if (ax1 > hx0 && ax0 < hx1 && ay1 > hy0 && ay0 < hy1) {
        e.attack_hit_cd = static_cast<float>(std::max(1, ObjIntAttr(eo, "atk_cooldown", 60))) / 60.0f;
        Damage();
        return;
      }
    }

    const bool overlap = px1 > ex0 && px0 < ex1 && py1 > ey0 && py0 < ey1;
    if (!overlap) continue;

    const bool stomp = player_.vy > 0.0f && pbottom <= ey0 + 12.0f;
    if (stomp) {
      e.dying = true;
      e.anim_time = 0.0f;
      e.frame = 0;
      if (e.death_action >= 0) e.action = e.death_action;
      player_.vy = -220.0f;
      score_ += 50;
      AudioPlay(Sfx::kCoin);
    } else {
      Damage();
      return;
    }
  }
}

int Game::ChunkTile(const SceneLayer& layer, int col, int row) const {
  const int cx = col >= 0 ? col / kP3dChunkCells
                          : -((-col + kP3dChunkCells - 1) / kP3dChunkCells);
  const int cy = row >= 0 ? row / kP3dChunkCells
                          : -((-row + kP3dChunkCells - 1) / kP3dChunkCells);
  const int lx = col - cx * kP3dChunkCells;
  const int ly = row - cy * kP3dChunkCells;
  for (const SceneChunk& c : layer.chunks) {
    if (c.cx != cx || c.cy != cy) continue;
    const size_t i = static_cast<size_t>(ly) * kP3dChunkCells + lx;
    if (i >= c.cells.size()) return -1;
    return c.cells[i];
  }
  return -1;
}


int Game::TileWallsAt(int col, int row) const {
  if (p3d_ == nullptr) return 0;
  const Scene& s = p3d_->scenes[scene_index_];
  const int cx = col >= 0 ? col / kP3dChunkCells
                          : -((-col + kP3dChunkCells - 1) / kP3dChunkCells);
  const int cy = row >= 0 ? row / kP3dChunkCells
                          : -((-row + kP3dChunkCells - 1) / kP3dChunkCells);
  const int lx = col - cx * kP3dChunkCells;
  const int ly = row - cy * kP3dChunkCells;
  for (const SceneCollisionChunk& c : s.collision) {
    if (c.cx != cx || c.cy != cy) continue;
    const size_t i = static_cast<size_t>(ly) * kP3dChunkCells + lx;
    if (i >= c.masks.size()) return 0;
    return c.masks[i];
  }
  return 0;
}

bool Game::PlayerAttachPoint(const char* name, float* x, float* y) const {
  if (p3d_ == nullptr) return false;
  const Object& o = p3d_->objects[player_object_];
  if (player_.action < 0 || player_.action >= static_cast<int>(o.actions.size())) {
    return false;
  }
  const Action& a = o.actions[player_.action];
  if (player_.frame < 0 || player_.frame >= static_cast<int>(a.frames.size())) {
    return false;
  }
  for (const AttachPoint& ap : a.frames[player_.frame].attach) {
    if (ap.name == name) {
      *x = player_.x + static_cast<float>(ap.x);
      *y = player_.y + static_cast<float>(ap.y);
      return true;
    }
  }
  return false;
}

void Game::ResolveX(float dx, int obj_index) {
  if (dx == 0.0f || p3d_ == nullptr || obj_index < 0 ||
      obj_index >= static_cast<int>(p3d_->objects.size())) {
    return;
  }
  const Object& o = p3d_->objects[obj_index];
  const AttackBox cb = ColBoxOf(o, player_.action, player_.frame);
  const float old = player_.x;
  player_.x += dx;
  const float top = player_.y + static_cast<float>(cb.y);
  const float bottom = top + static_cast<float>(cb.h);
  const float left = player_.x + static_cast<float>(cb.x);
  const float right = left + static_cast<float>(cb.w);
  const float old_left = old + static_cast<float>(cb.x);
  const float old_right = old_left + static_cast<float>(cb.w);
  const int r0 = static_cast<int>(std::floor(top / 8.0f));
  const int r1 = static_cast<int>(std::floor((bottom - 0.001f) / 8.0f));
  float edge = 0.0f;
  bool hit = false;
  if (dx > 0.0f) {
    for (int r = r0; r <= r1; ++r) {
      const float ty0 = static_cast<float>(r * 8);
      const float ty1 = ty0 + 8.0f;
      if (top >= ty1 - 0.001f || bottom <= ty0 + 0.001f) continue;
      const int c0 = static_cast<int>(std::floor(old_right / 8.0f));
      const int c1 = static_cast<int>(std::floor((right - 0.001f) / 8.0f));
      for (int c = c0; c <= c1; ++c) {
        const float tx = static_cast<float>(c * 8);
        if (old_right <= tx + 0.001f && right > tx + 0.001f &&
            (TileWallsAt(c, r) & 8) != 0) {
          if (!hit || tx < edge) {
            edge = tx;
            hit = true;
          }
        }
      }
    }
    if (hit) player_.x = edge - static_cast<float>(cb.x + cb.w);
  } else {
    for (int r = r0; r <= r1; ++r) {
      const float ty0 = static_cast<float>(r * 8);
      const float ty1 = ty0 + 8.0f;
      if (top >= ty1 - 0.001f || bottom <= ty0 + 0.001f) continue;
      const int c0 = static_cast<int>(std::floor((left - 0.001f) / 8.0f));
      const int c1 = static_cast<int>(std::floor(old_left / 8.0f));
      for (int c = c0; c <= c1; ++c) {
        const float tx = static_cast<float>((c + 1) * 8);
        if (old_left >= tx - 0.001f && left < tx - 0.001f &&
            (TileWallsAt(c, r) & 2) != 0) {
          if (!hit || tx > edge) {
            edge = tx;
            hit = true;
          }
        }
      }
    }
    if (hit) player_.x = edge - static_cast<float>(cb.x);
  }
  if (hit) player_.vx = 0.0f;
}

void Game::ResolveY(float dy, int obj_index) {
  if (dy == 0.0f || p3d_ == nullptr || obj_index < 0 ||
      obj_index >= static_cast<int>(p3d_->objects.size())) {
    return;
  }
  const Object& o = p3d_->objects[obj_index];
  const AttackBox cb = ColBoxOf(o, player_.action, player_.frame);
  const float old = player_.y;
  player_.y += dy;
  const float left = player_.x + static_cast<float>(cb.x);
  const float right = left + static_cast<float>(cb.w);
  const float old_top = old + static_cast<float>(cb.y);
  const float old_bottom = old_top + static_cast<float>(cb.h);
  const float top = player_.y + static_cast<float>(cb.y);
  const float bottom = top + static_cast<float>(cb.h);
  const int c0 = static_cast<int>(std::floor(left / 8.0f));
  const int c1 = static_cast<int>(std::floor((right - 0.001f) / 8.0f));
  float edge = 0.0f;
  bool hit = false;
  if (dy > 0.0f) {
    for (int c = c0; c <= c1; ++c) {
      const float tx0 = static_cast<float>(c * 8);
      const float tx1 = tx0 + 8.0f;
      if (left >= tx1 - 0.001f || right <= tx0 + 0.001f) continue;
      const int r0 = static_cast<int>(std::floor(old_bottom / 8.0f));
      const int r1 = static_cast<int>(std::floor((bottom - 0.001f) / 8.0f));
      for (int r = r0; r <= r1; ++r) {
        const float ty = static_cast<float>(r * 8);
        if (old_bottom <= ty + 0.001f && bottom > ty + 0.001f &&
            (TileWallsAt(c, r) & 1) != 0) {
          if (!hit || ty < edge) {
            edge = ty;
            hit = true;
          }
        }
      }
    }
    if (hit) {
      player_.y = edge - static_cast<float>(cb.y + cb.h);
      player_.on_ground = true;
      player_.vy = 0.0f;
    }
  } else {
    for (int c = c0; c <= c1; ++c) {
      const float tx0 = static_cast<float>(c * 8);
      const float tx1 = tx0 + 8.0f;
      if (left >= tx1 - 0.001f || right <= tx0 + 0.001f) continue;
      const int r0 = static_cast<int>(std::floor((top - 0.001f) / 8.0f));
      const int r1 = static_cast<int>(std::floor(old_top / 8.0f));
      for (int r = r0; r <= r1; ++r) {
        const float ty = static_cast<float>((r + 1) * 8);
        if (old_top >= ty - 0.001f && top < ty - 0.001f &&
            (TileWallsAt(c, r) & 4) != 0) {
          if (!hit || ty > edge) {
            edge = ty;
            hit = true;
          }
        }
      }
    }
    if (hit) {
      player_.y = edge - static_cast<float>(cb.y);
      player_.vy = 0.0f;
    }
  }
}

bool Game::CheckCondition(const Condition& c, const eng::InputState& input) const {
  auto down = [&input](const char* key) {
    if (std::strcmp(key, "right") == 0) return eng::KeyDown(input, eng::Key::kRight);
    if (std::strcmp(key, "left") == 0) return eng::KeyDown(input, eng::Key::kLeft);
    if (std::strcmp(key, "up") == 0) return eng::KeyDown(input, eng::Key::kUp);
    if (std::strcmp(key, "down") == 0) return eng::KeyDown(input, eng::Key::kDown);
    if (std::strcmp(key, "jump") == 0) return eng::KeyDown(input, eng::Key::kA);
    if (std::strcmp(key, "attack") == 0) return eng::KeyDown(input, eng::Key::kB);
    return false;
  };
  auto pressed = [&input](const char* key) {
    if (std::strcmp(key, "jump") == 0) return eng::KeyPressed(input, eng::Key::kA);
    if (std::strcmp(key, "attack") == 0) return eng::KeyPressed(input, eng::Key::kB);
    return false;
  };
  switch (c.type) {
    case 0:  // key
      if (std::strcmp(c.key.c_str(), "jump") == 0) return pressed("jump");
      return down(c.key.c_str());
    case 1:  // not_key
      if (std::strcmp(c.key.c_str(), "jump") == 0) return !down("jump");
      return !down(c.key.c_str());
    case 2: return player_.on_ground;
    case 3: return !player_.on_ground;
    case 4: return player_.anim_end;
    case 5: return player_.hp <= 0;
    case 6: return true;  // always
    case 7:  // timer ticks
      return player_.anim_time * 60.0f >= static_cast<float>(c.value);
    case 8: return score_ >= c.value;
    case 9: return lives_ <= c.value;
    case 10: {
      auto it = variables_.find(c.key);
      return (it == variables_.end() ? 0 : it->second) == c.value;
    }
    default: return false;
  }
}

void Game::UpdateAction(const eng::InputState& input) {
  if (p3d_ == nullptr || player_object_ < 0) return;
  const Object& o = p3d_->objects[player_object_];
  if (o.actions.empty()) return;
  if (player_.action < 0 || player_.action >= static_cast<int>(o.actions.size())) {
    player_.action = o.default_action;
  }
  for (int step = 0; step < 8; ++step) {
    bool changed = false;
    for (const Link& link : o.links) {
      if (link.from != player_.action) continue;
      bool ok = !link.conditions.empty();
      for (const Condition& c : link.conditions) {
        if (!CheckCondition(c, input)) { ok = false; break; }
      }
      if (!ok) continue;
      if (link.to >= 0 && link.to < static_cast<int>(o.actions.size()) &&
          link.to != player_.action) {
        player_.action = link.to;
        player_.anim_time = 0.0f;
        player_.frame = 0;
        player_.anim_end = false;
        changed = true;
        break;
      }
    }
    if (!changed) break;
  }
}

void Game::UpdateAnimation(float dt) {
  if (p3d_ == nullptr || player_object_ < 0) return;
  const Object& o = p3d_->objects[player_object_];
  if (o.actions.empty()) return;
  if (player_.action < 0 || player_.action >= static_cast<int>(o.actions.size())) {
    player_.action = o.default_action;
  }
  const Action& a = o.actions[player_.action];
  if (a.frames.empty()) {
    player_.frame = 0;
    player_.anim_end = true;
    return;
  }
  player_.anim_time += dt;
  bool ended = false;
  player_.frame = ActionFrameAt(a, player_.anim_time, &ended);
  player_.anim_end = ended;
}

bool Game::RunTriggerList(const Trigger& trigger,
                          const std::vector<TriggerAction>& list) {
  bool has_win = false;
  for (const TriggerAction& a : list) {
    if (a.type == 3) has_win = true;
  }
  for (size_t ai = 0; ai < list.size(); ++ai) {
    const TriggerAction& action = list[ai];
    if (action.type == 1) {  // set_respawn
      respawn_x_ = static_cast<float>(trigger.x + trigger.w / 2);
      respawn_y_ = static_cast<float>(trigger.y + trigger.h / 2);
      has_respawn_ = true;
      continue;
    }
    if (action.type == 2) {  // set_variable
      variables_[action.target] = action.value;
      continue;
    }
    if (action.type == 11) {  // save
      SaveToFile(save_path_);
      continue;
    }
    if (action.type == 12) {  // load
      if (LoadFromFile(save_path_)) return true;
      continue;
    }
    if (action.type == 9) {  // open_ui
      OpenUiById(action.target);
      if (UiModalActive() && !modal_stack_.empty()) {
        const int top = modal_stack_.back();
        if (top >= 0 && top < static_cast<int>(p3d_->ui_screens.size()) &&
            p3d_->ui_screens[static_cast<size_t>(top)].dismiss != 3) {
          pending_trigger_ = trigger;
          pending_actions_.assign(list.begin() + static_cast<std::ptrdiff_t>(ai) + 1,
                                  list.end());
          has_pending_ui_ = true;
          return true;
        }
      }
      continue;
    }
    if (action.type == 10) {  // close_ui
      RequestCloseUi();
      continue;
    }
    if (action.type == 0) {  // change_scene
      // 同一触发器里同时有胜利时，胜利优先；否则切场景会把 win 吞掉。
      if (has_win) continue;
      int target = -1;
      for (int i = 0; i < static_cast<int>(p3d_->scenes.size()); ++i) {
        if (p3d_->scenes[i].id == action.target) {
          target = i;
          break;
        }
      }
      if (target < 0) continue;
      const int hp = player_.hp;
      const int score = score_;
      ++transition_count_;
      SetScene(target);
      player_.hp = hp;
      score_ = score;
      return true;
    }
    if (action.type == 7) {  // attr_add：改玩家属性（hp 同步）
      if (!action.target.empty()) {
        int& v = player_.attrs[action.target];
        v += action.value;
        if (action.target == "hp") player_.hp = v;
      }
      continue;
    }
    if (action.type == 6) {  // play_sfx
      PlayClipById(action.target);
      continue;
    }
    if (action.type == 8) {  // spawn
      SpawnById(action.target, player_.x, player_.y, player_.facing, false);
      continue;
    }
    if (action.type == 3) {  // win：胜利横幅由渲染层统一画，别再挂一条 toast
      won_ = true;
      toast_.clear();
      toast_timer_ = 0.0f;
      continue;
    }
    if (action.type == 4) {  // lose：同上，避免 GAME OVER 出现两份
      game_over_ = true;
      toast_.clear();
      toast_timer_ = 0.0f;
      continue;
    }
  }
  return false;
}

bool Game::RunTriggerActions(const Trigger& trigger) {
  return RunTriggerList(trigger, trigger.actions);
}

bool Game::RunTriggerExitActions(const Trigger& trigger) {
  RunTriggerList(trigger, trigger.exit_actions);
  return false;
}

bool Game::RunTriggerInsideActions(const Trigger& trigger) {
  return RunTriggerList(trigger, trigger.inside_actions);
}

void Game::UpdateTriggers() {
  if (p3d_ == nullptr || player_object_ < 0 || won_) return;
  const Scene& s = p3d_->scenes[scene_index_];
  if (trigger_done_.size() != s.triggers.size()) {
    trigger_done_.assign(s.triggers.size(), 0);
  }
  if (trigger_inside_.size() != s.triggers.size()) {
    trigger_inside_.assign(s.triggers.size(), 0);
  }
  if (trigger_tick_.size() != s.triggers.size()) {
    trigger_tick_.assign(s.triggers.size(), 0);
  }
  const Object& po = p3d_->objects[player_object_];
  auto overlaps = [](float x0, float y0, float x1, float y1, const Trigger& t) {
    return !(x1 <= static_cast<float>(t.x) || x0 >= static_cast<float>(t.x + t.w) ||
             y1 <= static_cast<float>(t.y) || y0 >= static_cast<float>(t.y + t.h));
  };
  auto rule_ok = [&](const TriggerRule& rule) {
    if (!rule.enabled) return false;
    if (rule.require_variable.empty()) return true;
    auto it = variables_.find(rule.require_variable);
    const int value = it == variables_.end() ? 0 : it->second;
    return value == rule.require_value;
  };
  for (size_t i = 0; i < s.triggers.size(); ++i) {
    const Trigger& t = s.triggers[i];
    auto run_rules = [&](int mode) {
      for (const TriggerRule& rule : t.rules) {
        if (rule.mode != mode || !rule_ok(rule)) continue;
        if (RunTriggerList(t, rule.actions)) return true;
      }
      return false;
    };
    bool overlap = false;
    if (t.target_object >= 0) {
      for (const SceneObject& so : s.objects) {
        if (so.object != t.target_object) continue;
        if (t.target_object == player_object_) {
          const float x0 = player_.x + static_cast<float>(po.col_x);
          const float y0 = player_.y + static_cast<float>(po.col_y);
          overlap = overlaps(x0, y0, x0 + static_cast<float>(po.col_w),
                             y0 + static_cast<float>(po.col_h), t);
        } else {
          const Object& to = p3d_->objects[static_cast<size_t>(so.object)];
          const float x0 = static_cast<float>(so.x) + static_cast<float>(to.col_x);
          const float y0 = static_cast<float>(so.y) + static_cast<float>(to.col_y);
          overlap = overlaps(x0, y0, x0 + static_cast<float>(to.col_w),
                             y0 + static_cast<float>(to.col_h), t);
        }
        if (overlap) break;
      }
    } else {
      const float px0 = player_.x + static_cast<float>(po.col_x);
      const float px1 = px0 + static_cast<float>(po.col_w);
      const float py0 = player_.y + static_cast<float>(po.col_y);
      const float py1 = py0 + static_cast<float>(po.col_h);
      overlap = overlaps(px0, py0, px1, py1, t);
    }
    const bool was_inside = trigger_inside_[i] != 0;
    trigger_inside_[i] = overlap ? 1 : 0;
    if (overlap) {
      if (!was_inside) trigger_tick_[i] = 0;
      ++trigger_tick_[i];
      if (t.inside_interval > 0 &&
          (trigger_tick_[i] % static_cast<unsigned>(t.inside_interval)) == 0) {
        if (run_rules(2)) return;
        if (!t.inside_actions.empty() && RunTriggerInsideActions(t)) return;
      }
    } else {
      trigger_tick_[i] = 0;
    }
    // 离开：跑 exit rules / exit_actions（不判断 once）
    if (!overlap && was_inside) {
      if (run_rules(1)) return;
      if (!t.exit_actions.empty() && RunTriggerExitActions(t)) return;
    }
    if (!overlap) continue;
    if (t.once && trigger_done_[i] != 0) continue;
    if (!t.require_variable.empty()) {
      auto it = variables_.find(t.require_variable);
      const int value = it == variables_.end() ? 0 : it->second;
      if (value != t.require_value) continue;
    }
    if (was_inside && t.once) continue;
    trigger_done_[i] = 1;
    if (run_rules(0)) return;
    if (RunTriggerActions(t)) return;
  }
}

// 规则里的「生成」：按对象 id 生成一发，速度沿朝向（子弹速度取目标对象的 bullet_speed）
void Game::SpawnById(const std::string& id, float x, float y, int dir, bool from_player) {
  if (p3d_ == nullptr) return;
  for (size_t i = 0; i < p3d_->objects.size(); ++i) {
    const Object& target = p3d_->objects[i];
    if (target.id != id) continue;
    ProjectileState p;
    p.object = static_cast<int>(i);
    p.x = x;
    p.y = y;
    const float sp = static_cast<float>(ObjIntAttr(target, "bullet_speed", 120));
    p.vx = (dir < 0 ? -1.0f : 1.0f) * sp;
    p.vy = 0.0f;
    p.life = 3.0f;
    p.damage = ObjIntAttr(target, "atk", 1);
    p.from_player = from_player;
    projectiles_.push_back(p);
    return;
  }
}

void Game::SpawnProjectiles(const Object& owner, float ox, float oy, float ovx, float ovy,
                            bool from_player, int parent_scene_object) {
  if (p3d_ == nullptr) return;
  for (const Spawner& sp : owner.spawners) {
    if (sp.object < 0 || sp.object >= static_cast<int>(p3d_->objects.size())) continue;
    const Object& target = p3d_->objects[sp.object];
    const float base_angle = std::atan2(sp.vy, sp.vx);
    const int count = std::max(1, sp.count);
    for (int i = 0; i < count; ++i) {
      const float spread = static_cast<float>(sp.spread) * 3.14159265f / 180.0f;
      const float t = count > 1 ? (static_cast<float>(i) / (count - 1) - 0.5f) : 0.0f;
      const float angle = base_angle + t * spread;
      ProjectileState p;
      p.object = sp.object;
      p.x = ox + static_cast<float>(sp.offset_x);
      p.y = oy + static_cast<float>(sp.offset_y);
      const float speed = std::sqrt(sp.vx * sp.vx + sp.vy * sp.vy);
      p.vx = std::cos(angle) * speed + (sp.inherit_velocity ? ovx : 0.0f);
      p.vy = std::sin(angle) * speed + (sp.inherit_velocity ? ovy : 0.0f);
      p.life = static_cast<float>(std::max(1, sp.lifetime)) / 60.0f;
      p.damage = ObjIntAttr(target, "atk", 1);
      p.parent_scene_object = parent_scene_object;
      p.destroy_on_hit = sp.destroy_on_hit;
      p.destroy_with_parent = sp.destroy_with_parent;
      p.from_player = from_player;
      projectiles_.push_back(p);
    }
  }
}

void Game::UpdateProjectiles(float dt) {
  if (p3d_ == nullptr) return;
  const Object& po = p3d_->objects[player_object_];
  auto hurt_box = [](const Object& o, int action, int frame) {
    AttackBox box{o.hurt_x, o.hurt_y, o.hurt_w, o.hurt_h};
    if (box.w <= 0 || box.h <= 0) box = ColBoxOf(o, action, frame);
    if (action >= 0 && action < static_cast<int>(o.actions.size())) {
      const Action& a = o.actions[action];
      if (frame >= 0 && frame < static_cast<int>(a.frames.size())) {
        const AttackBox& fb = a.frames[frame].hurt;
        if (fb.w > 0 && fb.h > 0) box = AttackBox{fb.x, fb.y, fb.w, fb.h};
      }
    }
    return box;
  };
  for (size_t i = 0; i < projectiles_.size();) {
    ProjectileState& p = projectiles_[i];
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.life -= dt;
    if (p.hit_cd > 0.0f) p.hit_cd -= dt;
    bool remove = p.life <= 0.0f;
    if (!remove && p.destroy_with_parent && p.parent_scene_object >= 0) {
      bool parent_dead = true;
      if (control_ == ControlProfile::kPlatformer) {
        for (const EnemyState& owner : enemies_) {
          if (owner.scene_object == p.parent_scene_object && !owner.dead && !owner.dying) {
            parent_dead = false;
            break;
          }
        }
      } else {
        for (const ShooterEnemy& owner : shooter_enemies_) {
          if (owner.scene_object == p.parent_scene_object && owner.alive) {
            parent_dead = false;
            break;
          }
        }
      }
      if (parent_dead) remove = true;
    }
    if (!remove && control_ == ControlProfile::kFree2d && p.hit_cd <= 0.0f &&
        p.object >= 0 && p.object < static_cast<int>(p3d_->objects.size())) {
      const Object& target = p3d_->objects[p.object];
      if (p.from_player) {
        for (ShooterEnemy& e : shooter_enemies_) {
          if (!e.alive) continue;
          if (e.object < 0 || e.object >= static_cast<int>(p3d_->objects.size())) continue;
          const Object& eo = p3d_->objects[e.object];
          if (ObjectTeam(eo) == ObjectTeam(target)) continue;
          const AttackBox hb{eo.hurt_w > 0 ? eo.hurt_x : eo.col_x,
                             eo.hurt_h > 0 ? eo.hurt_y : eo.col_y,
                             eo.hurt_w > 0 ? eo.hurt_w : eo.col_w,
                             eo.hurt_h > 0 ? eo.hurt_h : eo.col_h};
          const float ex0 = e.x + static_cast<float>(hb.x);
          const float ey0 = e.y + static_cast<float>(hb.y);
          const float ex1 = ex0 + static_cast<float>(hb.w);
          const float ey1 = ey0 + static_cast<float>(hb.h);
          if (p.x <= ex0 || p.x >= ex1 || p.y <= ey0 || p.y >= ey1) continue;
          e.alive = false;
          score_ += ObjIntAttr(eo, "score", 100);
          if (shooter_effect_object_ >= 0) {
            ShooterExplosion fx;
            fx.object = shooter_effect_object_;
            fx.x = e.x;
            fx.y = e.y;
            shooter_explosions_.push_back(fx);
          }
          if (p.destroy_on_hit) remove = true; else p.hit_cd = 0.2f;
          break;
        }
      } else {
        AttackBox phb{po.hurt_x, po.hurt_y, po.hurt_w, po.hurt_h};
        if (phb.w <= 0 || phb.h <= 0) {
          phb = ColBoxOf(po, player_.action, player_.frame);
        }
        if (player_.action >= 0 && player_.action < static_cast<int>(po.actions.size())) {
          const Action& pa = po.actions[player_.action];
          if (player_.frame >= 0 && player_.frame < static_cast<int>(pa.frames.size())) {
            const AttackBox& fb = pa.frames[player_.frame].hurt;
            if (fb.w > 0 && fb.h > 0) phb = AttackBox{fb.x, fb.y, fb.w, fb.h};
          }
        }
        const float px0 = player_.x + static_cast<float>(phb.x);
        const float py0 = player_.y + static_cast<float>(phb.y);
        const float px1 = px0 + static_cast<float>(phb.w);
        const float py1 = py0 + static_cast<float>(phb.h);
        if (ObjectTeam(po) != ObjectTeam(target) &&
            p.x > px0 && p.x < px1 && p.y > py0 && p.y < py1) {
          HShooterHitPlayer();
          // 受击可能重置场景（清空 projectiles_），当前引用已失效
          return;
        }
      }
    }
    if (!remove && control_ == ControlProfile::kFree2d) {
      if (remove) {
        projectiles_.erase(projectiles_.begin() + i);
      } else {
        ++i;
      }
      continue;
    }
    if (!remove && p.hit_cd <= 0.0f && p.object >= 0 &&
        p.object < static_cast<int>(p3d_->objects.size())) {
      const Object& target = p3d_->objects[p.object];
      if (p.from_player) {
        for (EnemyState& e : enemies_) {
          if (e.dying || e.dead || e.hit_invuln > 0.0f) continue;
          const Object& eo = p3d_->objects[e.object];
          if (ObjectTeam(eo) == ObjectTeam(target)) continue;
          const AttackBox hb = hurt_box(eo, e.action, e.frame);
          const float ex0 = e.x + static_cast<float>(hb.x);
          const float ey0 = e.y + static_cast<float>(hb.y);
          const float ex1 = ex0 + static_cast<float>(hb.w);
          const float ey1 = ey0 + static_cast<float>(hb.h);
          if (p.x <= ex0 || p.x >= ex1 || p.y <= ey0 || p.y >= ey1) continue;
          e.hp -= p.damage;
          e.hit_invuln = static_cast<float>(std::max(0, ObjIntAttr(eo, "invuln", 0))) / 60.0f;
          e.hit_flash = 0.15f;
          if (e.hp <= 0) {
            e.dying = true;
            e.anim_time = 0.0f;
            e.frame = 0;
            if (e.death_action >= 0) e.action = e.death_action;
            score_ += ObjIntAttr(eo, "score", 100);
          }
          if (p.destroy_on_hit) {
            remove = true;
          } else {
            p.hit_cd = 0.2f;
          }
          break;
        }
      } else {
        const AttackBox hb = hurt_box(po, player_.action, player_.frame);
        const float px0 = player_.x + static_cast<float>(hb.x);
        const float py0 = player_.y + static_cast<float>(hb.y);
        const float px1 = px0 + static_cast<float>(hb.w);
        const float py1 = py0 + static_cast<float>(hb.h);
        if (ObjectTeam(po) != ObjectTeam(target) &&
            p.x > px0 && p.x < px1 && p.y > py0 && p.y < py1) {
          Damage();
          // Damage 会 SetScene（清空 projectiles_），这里的引用已经悬空，直接收工
          return;
        }
      }
    }
    if (remove) {
      projectiles_.erase(projectiles_.begin() + i);
    } else {
      ++i;
    }
  }
}

void Game::RenderProjectiles(eng::DrawList& dl, const eng::TextureId* textures,
                             int texture_count) const {
  if (p3d_ == nullptr) return;
  const float ox = control_ == ControlProfile::kPlatformer ? camera_x_ : (camera_profile_ == CameraProfile::kAutoScroll ? hscroll_x_ : 0.0f);
  const float oy = control_ == ControlProfile::kPlatformer ? camera_y_ : 0.0f;
  for (const ProjectileState& p : projectiles_) {
    if (p.object < 0 || p.object >= static_cast<int>(p3d_->objects.size())) continue;
    const Object& o = p3d_->objects[p.object];
    if (o.image < 0 || o.image >= texture_count || textures[o.image] == 0) continue;
    int frame_no = 0;
    int action = o.default_action;
    if (action >= 0 && action < static_cast<int>(o.actions.size())) {
      const Action& a = o.actions[action];
      if (!a.frames.empty()) frame_no = a.frames[0].image;
    }
    const int fw = std::max(1, o.frame_w);
    const int fh = std::max(1, o.frame_h);
    const int tex_w = o.image < static_cast<int>(p3d_->images.size())
                          ? p3d_->images[o.image].width
                          : fw;
    const int tc = (tex_w / fw > 0) ? frame_no % (tex_w / fw) : 0;
    const int tr = (tex_w / fw > 0) ? frame_no / (tex_w / fw) : 0;
    dl.DrawImage(eng::Rect2{
                     eng::Vec2{p.x - ox - fw * 0.5f,
                               p.y - oy - fh * 0.5f},
                     eng::Vec2{static_cast<float>(fw), static_cast<float>(fh)}},
                 textures[o.image], p.from_player ? 0xFFFFFFFFu : 0xFF80A0FFu,
                 eng::TexRegion::Make(tc * fw, tr * fh, fw, fh));
  }
}

void Game::Damage() {
  if (invuln_ > 0.0f) return;
  if (player_.hp > 0) --player_.hp;
  const int hp = player_.hp;
  const int score = score_;
  const float respawn_x = respawn_x_;
  const float respawn_y = respawn_y_;
  const bool has_respawn = has_respawn_;
  AudioPlay(Sfx::kHurt);
  SetScene(scene_index_);
  if (has_respawn) {
    player_.x = respawn_x;
    player_.y = respawn_y;
    respawn_x_ = respawn_x;
    respawn_y_ = respawn_y;
    has_respawn_ = true;
  }
  if (hp <= 0) {
    score_ = 0;
    player_.hp = player_.attrs.count("hp") ? player_.attrs.at("hp") : 100;
  } else {
    player_.hp = hp;
    score_ = score;
    toast_ = "OUCH";
    toast_timer_ = 1.2f;
  }
  invuln_ = 1.5f;
}

void Game::UpdateCamera(float dt) {
  if (p3d_ == nullptr) return;
  const Scene& s = p3d_->scenes[scene_index_];
  // ★ 死区：角色在框里 → 相机不动；出框才推 ★
  //   以前的写法先把 desired 设成"居中跟随"，死区只在出框时改它，
  //   结果框内相机会偷偷跟过去 —— 死区等于没生效。
  float desired_x = camera_x_;
  float desired_y = camera_y_;
  if (camera_profile_ == CameraProfile::kFollow) {
  if (s.camera_deadzone_w > 0) {
    const float screen_x = player_.x - camera_x_;
    const float left = view_w_ * 0.5f - static_cast<float>(s.camera_deadzone_w) * 0.5f;
    const float right = view_w_ * 0.5f + static_cast<float>(s.camera_deadzone_w) * 0.5f;
    if (screen_x < left) desired_x = player_.x - left;
    else if (screen_x > right) desired_x = player_.x - right;
  } else {
    desired_x = player_.x - view_w_ * 0.5f + static_cast<float>(s.camera_offset_x);
  }
  if (s.camera_deadzone_h > 0) {
    const float screen_y = player_.y - camera_y_;
    const float top = view_h_ * 0.5f - static_cast<float>(s.camera_deadzone_h) * 0.5f;
    const float bottom = view_h_ * 0.5f + static_cast<float>(s.camera_deadzone_h) * 0.5f;
    if (screen_y < top) desired_y = player_.y - top;
    else if (screen_y > bottom) desired_y = player_.y - bottom;
  } else {
    desired_y = player_.y - view_h_ * 0.5f + static_cast<float>(s.camera_offset_y);
  }
  }
  // ★ 世界矩形按所有 chunk 的 min/max 算，不能只看 max ★
  //   地图往左/上扩到负坐标时，只取 max 会把相机钳在 0，
  //   x<0 的内容永远进不了视野（无限地图的一半就废了）。
  int min_c = 0, min_r = 0, max_c = 0, max_r = 0;
  bool any_chunk = false;
  for (const SceneLayer& l : s.layers) {
    for (const SceneChunk& c : l.chunks) {
      const int c0 = c.cx * kP3dChunkCells;
      const int r0 = c.cy * kP3dChunkCells;
      const int c1 = c0 + kP3dChunkCells;
      const int r1 = r0 + kP3dChunkCells;
      if (!any_chunk) {
        min_c = c0; max_c = c1; min_r = r0; max_r = r1; any_chunk = true;
      } else {
        min_c = std::min(min_c, c0); max_c = std::max(max_c, c1);
        min_r = std::min(min_r, r0); max_r = std::max(max_r, r1);
      }
    }
  }
  const float world_l = static_cast<float>(min_c) * 8.0f;
  const float world_t = static_cast<float>(min_r) * 8.0f;
  const float world_r = static_cast<float>(max_c) * 8.0f;
  const float world_b = static_cast<float>(max_r) * 8.0f;
  const float ww = world_r - world_l;
  const float wh = world_b - world_t;
  if (camera_profile_ == CameraProfile::kFixed) {
    camera_x_ = world_l - (static_cast<float>(view_w_) - ww) * 0.5f;
    camera_y_ = world_t - (static_cast<float>(view_h_) - wh) * 0.5f;
    return;
  }
  if (camera_profile_ != CameraProfile::kFollow) return;
  if (ww > static_cast<float>(view_w_)) {
    desired_x = std::clamp(desired_x, world_l, world_r - static_cast<float>(view_w_));
  } else {
    desired_x = world_l - (static_cast<float>(view_w_) - ww) * 0.5f;
  }
  if (wh > static_cast<float>(view_h_)) {
    desired_y = std::clamp(desired_y, world_t, world_b - static_cast<float>(view_h_));
  } else {
    desired_y = world_t - (static_cast<float>(view_h_) - wh) * 0.5f;
  }
  const float t = 1.0f - std::exp(-8.0f * dt);
  camera_x_ += (desired_x - camera_x_) * t;
  camera_y_ += (desired_y - camera_y_) * t;
}

namespace {
int KeyEnum(const char* name) {
  if (std::strcmp(name, "left") == 0) return static_cast<int>(eng::Key::kLeft);
  if (std::strcmp(name, "right") == 0) return static_cast<int>(eng::Key::kRight);
  if (std::strcmp(name, "up") == 0) return static_cast<int>(eng::Key::kUp);
  if (std::strcmp(name, "down") == 0) return static_cast<int>(eng::Key::kDown);
  if (std::strcmp(name, "jump") == 0) return static_cast<int>(eng::Key::kA);
  if (std::strcmp(name, "attack") == 0) return static_cast<int>(eng::Key::kB);
  return -1;
}
bool KeyUpOf(const char* name, const eng::InputState& prev,
             const eng::InputState& cur) {
  const int k = KeyEnum(name);
  if (k < 0) return false;
  return prev.down[k] && !cur.down[k];
}
bool KeyOf(const char* name, const eng::InputState& in) {
  if (std::strcmp(name, "left") == 0) return eng::KeyDown(in, eng::Key::kLeft);
  if (std::strcmp(name, "right") == 0) return eng::KeyDown(in, eng::Key::kRight);
  if (std::strcmp(name, "up") == 0) return eng::KeyDown(in, eng::Key::kUp);
  if (std::strcmp(name, "down") == 0) return eng::KeyDown(in, eng::Key::kDown);
  if (std::strcmp(name, "jump") == 0) return eng::KeyDown(in, eng::Key::kA);
  if (std::strcmp(name, "attack") == 0) return eng::KeyDown(in, eng::Key::kB);
  return false;
}
bool KeyPressOf(const char* name, const eng::InputState& in) {
  if (std::strcmp(name, "left") == 0) return eng::KeyPressed(in, eng::Key::kLeft);
  if (std::strcmp(name, "right") == 0) return eng::KeyPressed(in, eng::Key::kRight);
  if (std::strcmp(name, "up") == 0) return eng::KeyPressed(in, eng::Key::kUp);
  if (std::strcmp(name, "down") == 0) return eng::KeyPressed(in, eng::Key::kDown);
  if (std::strcmp(name, "jump") == 0) return eng::KeyPressed(in, eng::Key::kA);
  if (std::strcmp(name, "attack") == 0) return eng::KeyPressed(in, eng::Key::kB);
  return false;
}
}  // namespace

// 规则驱动：条件读输入/状态，效果写 vx/vy/动作/朝向。
void Game::RunPlayerRules(const Object& o, const eng::InputState& input, float dt) {
  (void)dt;
  int want_anim = -1;
  int want_flip = 0;  // -1 左, 1 右, 0 不动
  bool moved = false;
  for (const Rule& r : o.rules) {
    if (!r.enabled) continue;
    bool hit = true;
    for (const RuleCond& c : r.when) {
      const bool ok = [&]() {
        switch (c.type) {
          case 0: return true;
          case 1: return KeyOf(c.key.c_str(), input);
          case 2: return KeyPressOf(c.key.c_str(), input);
          case 3:  // key_up
            return KeyUpOf(c.key.c_str(), prev_input_, input);
          case 4: return player_.on_ground;
          case 5: return !player_.on_ground;
          case 7: {
            auto it = player_.attrs.find(c.name);
            return it != player_.attrs.end() && it->second <= c.value;
          }
          case 6: {  // anim_end：最后一个画面且停留时间已过
            if (player_.action < 0 ||
                player_.action >= static_cast<int>(o.actions.size())) {
              return false;
            }
            const Action& a = o.actions[static_cast<size_t>(player_.action)];
            if (a.frames.empty()) return false;
            const size_t last = a.frames.size() - 1;
            if (player_.frame != static_cast<int>(last)) return false;
            return player_.anim_time >=
                   static_cast<float>(P3dActionHold(a, last)) / 60.0f;
          }
          case 8: {  // 属性比较（op: 0< 1<= 2= 3>= 4>）
            auto it = player_.attrs.find(c.name);
            const int v = it == player_.attrs.end() ? 0 : it->second;
            switch (c.op) {
              case 0: return v < c.value;
              case 2: return v == c.value;
              case 3: return v >= c.value;
              case 4: return v > c.value;
              default: return v <= c.value;
            }
          }
          case 9: return c.value > 0 && (tick_ % static_cast<uint32_t>(c.value)) == 0;
          case 10: return false;  // 玩家自己，没有“离玩家”的概念
          case 11: {  // key_held：按住超过 N 帧
            const int k = KeyEnum(c.key.c_str());
            return k >= 0 && key_hold_[k] >= c.value;
          }
          default: return false;
        }
      }();
      if (!ok) {
        hit = false;
        break;
      }
    }
    if (!hit) continue;
    for (const RuleEffect& e : r.effects) {
      switch (e.type) {
        case 0:  // move
          if (e.x != 0 || e.y != 0) {
            player_.vx = static_cast<float>(e.x);
            player_.vy = static_cast<float>(e.y);
            moved = true;
          }
          break;
        case 11:  // move_add：叠加到当前速度（惯性/击退）
          player_.vx += static_cast<float>(e.x);
          player_.vy += static_cast<float>(e.y);
          moved = true;
          break;
        case 12: {  // move_input：按方向键移动（8 向自动归一化）
          int ix = 0;
          int iy = 0;
          if (eng::KeyDown(input, eng::Key::kLeft)) --ix;
          if (eng::KeyDown(input, eng::Key::kRight)) ++ix;
          if (eng::KeyDown(input, eng::Key::kUp)) --iy;
          if (eng::KeyDown(input, eng::Key::kDown)) ++iy;
          const float sx = static_cast<float>(e.x != 0 ? e.x : o.speed);
          const float sy = static_cast<float>(e.y != 0 ? e.y : o.speed);
          float len = std::sqrt(static_cast<float>(ix * ix + iy * iy));
          if (len > 0.0f) {
            player_.vx = static_cast<float>(ix) / len * sx;
            player_.vy = static_cast<float>(iy) / len * sy;
            moved = true;
            if (ix != 0) player_.facing = ix > 0 ? 1 : -1;
          } else {
            player_.vx = 0.0f;
            player_.vy = 0.0f;
          }
          break;
        }
        case 1:  // move_px
          player_.x += static_cast<float>(e.x);
          player_.y += static_cast<float>(e.y);
          break;
        case 2:  // anim
          for (size_t ai = 0; ai < o.actions.size(); ++ai) {
            if (o.actions[ai].name == e.text) want_anim = static_cast<int>(ai);
          }
          break;
        case 3:  // flip
          if (e.text == "left") want_flip = -1;
          if (e.text == "right") want_flip = 1;
          break;
        case 5:  // sfx
          PlayClipById(e.text);
          break;
        case 6:  // spawn
          SpawnById(e.text, player_.x + static_cast<float>(e.x),
                    player_.y + static_cast<float>(e.y), player_.facing, true);
          break;
        case 8:  // attr_add
        case 9: {  // attr_set
          const int delta = e.type == 8 ? e.y : 0;
          const int val = e.type == 9 ? e.y : 0;
          if (!e.text.empty()) {
            int& v = player_.attrs[e.text];
            v = e.type == 8 ? v + delta : val;
            if (e.text == "hp") player_.hp = v;
          }
          break;
        }
        case 10: {  // damage：攻击盒内的敌人按公式扣血
          if (player_.action < 0 ||
              player_.action >= static_cast<int>(o.actions.size())) {
            break;
          }
          const Action& pa = o.actions[static_cast<size_t>(player_.action)];
          if (player_.frame < 0 ||
              player_.frame >= static_cast<int>(pa.frames.size())) {
            break;
          }
          const AttackBox ab = pa.frames[static_cast<size_t>(player_.frame)].attack;
          if (ab.w <= 0 || ab.h <= 0) break;
          const float ax0 = player_.x + static_cast<float>(ab.x);
          const float ay0 = player_.y + static_cast<float>(ab.y);
          const float ax1 = ax0 + static_cast<float>(ab.w);
          const float ay1 = ay0 + static_cast<float>(ab.h);
          std::string expr = e.formula;
          if (expr.empty()) expr = "damage";
          const auto fit = p3d_->formulas.find(expr);
          const std::string src = fit != p3d_->formulas.end() ? fit->second
                                                              : "max(1,{atk}-{target.def})";
          for (EnemyState& en : enemies_) {
            if (en.dead || en.dying) continue;
            const Object& eo = p3d_->objects[static_cast<size_t>(en.object)];
            const AttackBox hb = ColBoxOf(eo, en.action, en.frame);
            const float hx0 = en.x + static_cast<float>(hb.x);
            const float hy0 = en.y + static_cast<float>(hb.y);
            const float hx1 = hx0 + static_cast<float>(hb.w);
            const float hy1 = hy0 + static_cast<float>(hb.h);
            if (ax1 <= hx0 || ax0 >= hx1 || ay1 <= hy0 || ay0 >= hy1) continue;
            const int dmg = r4::EvalExpr(src, player_.attrs, &en.attrs);
            en.hp -= dmg;
            en.attrs["hp"] = en.hp;
            en.hit_flash = 0.12f;
            if (en.hp <= 0) en.dying = true;
          }
          break;
        }
        default: break;
      }
    }
  }
  if (!moved) {
    player_.vx = 0.0f;
  }
  if (want_flip < 0) player_.facing = -1;
  if (want_flip > 0) player_.facing = 1;
  if (want_anim >= 0 && want_anim != player_.action) {
    player_.action = want_anim;
    player_.frame = 0;
    player_.anim_time = 0.0f;
  }
}

// 敌人规则：条件看自身状态，效果写 vx/vy/动作/朝向；位移直接作用在 e.x/e.y。
void Game::RunEnemyRules(const Object& o, EnemyState& e, float dt) {
  int want_anim = -1;
  int want_flip = 0;
  bool moved = false;
  for (const Rule& r : o.rules) {
    if (!r.enabled) continue;
    bool hit = true;
    for (const RuleCond& c : r.when) {
      const bool ok = [&]() {
        switch (c.type) {
          case 0: return true;
          case 4: return false;   // 敌人没有地面判定
          case 5: return true;
          case 7: {
            auto it = e.attrs.find(c.name);
            return it != e.attrs.end() && it->second <= c.value;
          }
          case 8: {
            auto it = e.attrs.find(c.name);
            const int v = it == e.attrs.end() ? 0 : it->second;
            switch (c.op) {
              case 0: return v < c.value;
              case 2: return v == c.value;
              case 3: return v >= c.value;
              case 4: return v > c.value;
              default: return v <= c.value;
            }
          }
          case 9: return c.value > 0 && (e.tick % static_cast<uint32_t>(c.value)) == 0;
          case 10: {  // 离玩家 < N 像素
            const float dx = e.x - player_.x;
            const float dy = e.y - player_.y;
            return dx * dx + dy * dy <=
                   static_cast<float>(c.value) * static_cast<float>(c.value);
          }
          default: return false;
        }
      }();
      if (!ok) {
        hit = false;
        break;
      }
    }
    if (!hit) continue;
    for (const RuleEffect& ef : r.effects) {
      switch (ef.type) {
        case 0:  // move
          e.vx = static_cast<float>(ef.x);
          e.vy = static_cast<float>(ef.y);
          moved = true;
          break;
        case 11:  // move_add
          e.vx += static_cast<float>(ef.x);
          e.vy += static_cast<float>(ef.y);
          moved = true;
          break;
        case 1:  // move_px
          e.x += static_cast<float>(ef.x);
          e.y += static_cast<float>(ef.y);
          break;
        case 2:  // anim
          for (size_t ai = 0; ai < o.actions.size(); ++ai) {
            if (o.actions[ai].name == ef.text) want_anim = static_cast<int>(ai);
          }
          break;
        case 3:  // flip
          if (ef.text == "left") want_flip = -1;
          if (ef.text == "right") want_flip = 1;
          break;
        case 5:
          PlayClipById(ef.text);
          break;
        case 6:  // spawn
          SpawnById(ef.text, e.x + static_cast<float>(ef.x),
                    e.y + static_cast<float>(ef.y), e.facing, false);
          break;
        case 8:  // attr_add
        case 9: {  // attr_set
          if (!ef.text.empty()) {
            int& v = e.attrs[ef.text];
            v = ef.type == 8 ? v + ef.y : ef.y;
            if (ef.text == "hp") e.hp = v;
          }
          break;
        }
        case 7:  // destroy
          e.dead = true;
          break;
        default: break;
      }
    }
  }
  if (moved) {
    e.x += e.vx * dt;
    e.y += e.vy * dt;
  }
  if (want_flip < 0) e.facing = -1;
  if (want_flip > 0) e.facing = 1;
  if (want_anim >= 0 && want_anim != e.action) {
    e.action = want_anim;
    e.frame = 0;
    e.anim_time = 0.0f;
  }
}

void Game::UpdatePlatformer(float dt, const eng::InputState& input) {
  if (p3d_ == nullptr || player_object_ < 0) return;
  if (won_) return;
  if (invuln_ > 0.0f) invuln_ -= dt;
  if (toast_timer_ > 0.0f) toast_timer_ -= dt;
  if (player_attack_active_ > 0.0f) player_attack_active_ -= dt;
  if (player_attack_cooldown_ > 0.0f) player_attack_cooldown_ -= dt;
  const Object& o = p3d_->objects[player_object_];

  // ★ 触发器 → 环境修饰器 ★
  //   对象所在位置命中哪些触发器，就把它们的修饰器收集起来，交给 ComposeStats 合成。
  env_mods_.clear();
  {
    const Scene& sc = p3d_->scenes[scene_index_];
    for (const Trigger& t : sc.triggers) {
      if (player_.x < static_cast<float>(t.x) || player_.x > static_cast<float>(t.x + t.w)) continue;
      if (player_.y < static_cast<float>(t.y) || player_.y > static_cast<float>(t.y + t.h)) continue;
      for (const TriggerMod& m : t.mods) {
        pgmmv::StatMod sm;
        sm.id = static_cast<pgmmv::StatId>(m.stat);
        sm.op = static_cast<pgmmv::StatOp>(m.op);
        sm.value = m.value;
        env_mods_.push_back(sm);
      }
    }
  }
  // ★ 速度/跳跃/重力改成读"合成后的参数" ★
  //   base 来自对象字段，修饰器（水/毒/装备/…）以后往环境里加，这里不用改。
  pgmmv::StatBlock st = pgmmv::DefaultStats();
  st[pgmmv::StatId::kSpeed] = static_cast<float>(o.speed);
  st[pgmmv::StatId::kGravity] = static_cast<float>(o.gravity);
  st = pgmmv::ComposeStats(st, env_mods_);
  // 持续伤害/回血：hp_rate 是「每秒」，这里按 dt 累积成整数扣加
  if (st[pgmmv::StatId::kHpRate] != 0.0f && player_.hp > 0) {
    hp_accum_ += st[pgmmv::StatId::kHpRate] * dt;
    while (hp_accum_ >= 1.0f) { player_.hp += 1; hp_accum_ -= 1.0f; }
    while (hp_accum_ <= -1.0f) { player_.hp -= 1; hp_accum_ += 1.0f; }
    if (player_.hp < 0) player_.hp = 0;
  }
  const float stat_gravity = st[pgmmv::StatId::kGravity];
  const float stat_maxfall = st[pgmmv::StatId::kMaxFall];
  player_.vy += stat_gravity * dt;  // 重力是常量，不需要写规则
  // 行为全部由规则驱动（没有规则 = 不动）
  RunPlayerRules(o, input, dt);
  if (player_.vy > stat_maxfall) player_.vy = stat_maxfall;
  player_.on_ground = false;
  ResolveX(player_.vx * dt, player_object_);
  ResolveY(player_.vy * dt, player_object_);
  if (o.limit_screen) {  // 按当前帧的碰撞盒夹在 240x160 内
    const AttackBox cb = ColBoxOf(o, player_.action, player_.frame);
    const float left = -static_cast<float>(cb.x);
    const float right = view_w_ - static_cast<float>(cb.x + cb.w);
    const float top = -static_cast<float>(cb.y);
    const float bottom = view_h_ - static_cast<float>(cb.y + cb.h);
    player_.x = std::max(left, std::min(right, player_.x));
    player_.y = std::max(top, std::min(bottom, player_.y));
    if (player_.x <= left || player_.x >= right) player_.vx = 0.0f;
    if (player_.y >= bottom) player_.vy = 0.0f;
  }

  UpdateInteraction(input);
  if (won_) return;
  const Object& pc = p3d_->objects[player_object_];
  for (CheckpointState& cp : checkpoints_) {
    if (cp.active || cp.object < 0 || cp.object >= static_cast<int>(p3d_->objects.size())) continue;
    const Object& co = p3d_->objects[cp.object];
    const float cx0 = cp.x + static_cast<float>(co.col_x);
    const float cx1 = cx0 + static_cast<float>(co.col_w);
    const float cy0 = cp.y + static_cast<float>(co.col_y);
    const float cy1 = cy0 + static_cast<float>(co.col_h);
    const float px0 = player_.x + static_cast<float>(pc.col_x);
    const float px1 = px0 + static_cast<float>(pc.col_w);
    const float py0 = player_.y + static_cast<float>(pc.col_y);
    const float py1 = py0 + static_cast<float>(pc.col_h);
    if (px1 > cx0 && px0 < cx1 && py1 > cy0 && py0 < cy1) {
      cp.active = true;
      respawn_x_ = cp.x;
      respawn_y_ = cp.y;
      has_respawn_ = true;
      toast_ = "CHECK";
      toast_timer_ = 1.0f;
      AudioPlay(Sfx::kCoin);
    }
  }
  // 玩家攻击由规则里的 damage 动作处理（用当帧的攻击盒），这里不再硬编码 B 键

  if (player_.y > 10000.0f) {
    invuln_ = 0.0f;
    Damage();
    return;
  }

  UpdateAction(input);
  UpdateAnimation(dt);
  UpdateEnemies(dt);
  UpdateProjectiles(dt);
  UpdateTriggers();
  // 属性表里的 hp 和玩家真实血量保持同步（属性表是数据源）
  if (player_.attrs.find("hp") != player_.attrs.end()) {
    player_.attrs["hp"] = player_.hp;
  }
  UpdateCamera(dt);
}

// 交互点：玩家在区域内且条件满足时，按键执行动作
void Game::UpdateInteraction(const eng::InputState& input) {
  if (eng::KeyPressed(input, eng::Key::kStart)) debug_on_ = !debug_on_;
  active_interaction_ = -1;
  if (p3d_ == nullptr) return;
  const Scene& sc = p3d_->scenes[scene_index_];
  for (size_t i = 0; i < sc.triggers.size(); ++i) {
    const Trigger& t = sc.triggers[i];
    if (!t.interaction.enabled) continue;
    if (i < interaction_done_.size() && interaction_done_[i] && t.interaction.once) continue;
    if (player_.x < static_cast<float>(t.x) || player_.x > static_cast<float>(t.x + t.w)) continue;
    if (player_.y < static_cast<float>(t.y) || player_.y > static_cast<float>(t.y + t.h)) continue;
    if (!t.interaction.require_variable.empty()) {
      auto it = variables_.find(t.interaction.require_variable);
      const int v = it == variables_.end() ? 0 : it->second;
      if (v != t.interaction.require_value) continue;
    }
    active_interaction_ = static_cast<int>(i);
    if (!t.interaction.prompt.empty()) {
      toast_ = t.interaction.prompt;
      toast_timer_ = std::max(toast_timer_, 0.1f);
    }
    break;
  }
  if (active_interaction_ < 0) return;
  const Trigger& t = sc.triggers[static_cast<size_t>(active_interaction_)];
  bool pressed = false;
  const std::string& k = t.interaction.key;
  if (k == "b") pressed = eng::KeyPressed(input, eng::Key::kB);
  else if (k == "a") pressed = eng::KeyPressed(input, eng::Key::kA);
  else if (k == "start") pressed = eng::KeyPressed(input, eng::Key::kStart);
  else pressed = eng::KeyPressed(input, eng::Key::kB);
  if (!pressed) return;
  if (interaction_done_.size() <= static_cast<size_t>(active_interaction_)) {
    interaction_done_.resize(sc.triggers.size(), 0);
  }
  interaction_done_[static_cast<size_t>(active_interaction_)] = 1;
  for (const TriggerAction& a : t.interaction.actions) {
    if (a.type == 0) {  // change_scene
      for (size_t si = 0; si < p3d_->scenes.size(); ++si) {
        if (p3d_->scenes[si].id == a.target) {
          SetScene(static_cast<int>(si));
          return;
        }
      }
    } else if (a.type == 2) {  // set_variable
      variables_[a.target] = a.value;
    } else if (a.type == 5) {  // dialogue → 用 toast 显示一句
      toast_ = a.target;
      toast_timer_ = 2.0f;
    } else if (a.type == 6) {  // play_sfx
      PlayClipById(a.target);
    }
  }
}

void Game::Update(float dt, const eng::InputState& input) {
  if (p3d_ == nullptr || player_object_ < 0) return;
  for (int i = 0; i < eng::kKeyCount; ++i) {
    key_hold_[i] = input.down[i] ? key_hold_[i] + 1 : 0;
  }
  ++frame_count_;
  ++tick_;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.05f) dt = 0.05f;
  if (UiModalActive()) {
    const int top = modal_stack_.back();
    if (top >= 0 && top < static_cast<int>(p3d_->ui_screens.size())) {
      const UiScreenData& screen = p3d_->ui_screens[static_cast<size_t>(top)];
      if (modal_closing_) {
        ++modal_close_fade_;
        if (modal_close_fade_ >= modal_close_total_) CloseUi();
        prev_input_ = input;
        return;
      }
      if (screen.dismiss == 2) {
        --modal_timer_;
        if (modal_timer_ <= 0) {
          RequestCloseUi();
          prev_input_ = input;
          return;
        }
      }
      if (modal_fade_ > 0) --modal_fade_;
      if (input.pressed[static_cast<int>(eng::Key::kUp)]) {
        MoveModalFocus(-1, true);
      }
      if (input.pressed[static_cast<int>(eng::Key::kDown)]) {
        MoveModalFocus(1, true);
      }
      if (input.pressed[static_cast<int>(eng::Key::kLeft)]) {
        MoveModalFocus(-1, false);
      }
      if (input.pressed[static_cast<int>(eng::Key::kRight)]) {
        MoveModalFocus(1, false);
      }
      if (input.pressed[static_cast<int>(eng::Key::kA)]) {
        if (modal_focus_ >= 0) {
          ActivateModalFocus();
        } else if (screen.dismiss == 0) {
          RequestCloseUi();
        }
      }
      if ((input.pressed[static_cast<int>(eng::Key::kB)] ||
           input.pressed[static_cast<int>(eng::Key::kStart)]) &&
          screen.dismiss != 3) {
        RequestCloseUi();
      }
    }
    prev_input_ = input;
    return;
  }
  if (control_ == ControlProfile::kFree2d &&
      camera_profile_ == CameraProfile::kFixed) {
    UpdateShooter(dt, input);
    UpdateProjectiles(dt);
    UpdateAnimation(dt);
    if (!won_) UpdateTriggers();
    prev_input_ = input;
    return;
  }
  if (control_ == ControlProfile::kFree2d &&
      camera_profile_ == CameraProfile::kAutoScroll) {
    UpdateHShooter(dt, input);
    UpdateProjectiles(dt);
    return;
  }
  UpdatePlatformer(dt, input);
  prev_input_ = input;  // 下一帧算 key_up 用
}

void Game::RenderShooter(eng::DrawList& dl, const eng::TextureId* textures,
                              int texture_count) const {
  dl.Clear(0xFF000000u);
  for (int i = 0; i < 90; ++i) {
    const int x = (i * 97 + static_cast<int>(shooter_time_ * 26.0f)) % 400;
    const int y = (i * 53 + static_cast<int>(shooter_time_ * 17.0f)) % 240;
    const bool bright = (i % 5) == 0;
    dl.DrawRect(eng::Rect2{eng::Vec2{static_cast<float>(x), static_cast<float>(y)},
                           eng::Vec2{bright ? 2.0f : 1.0f, bright ? 2.0f : 1.0f}},
                bright ? 0xFFE8F2FFu : 0xFF8FA4C0u);
  }

  auto draw_obj = [&](int obj_index, float x, float y, int action_index,
                      int frame_index, eng::u32 tint) {
    if (obj_index < 0 || obj_index >= static_cast<int>(p3d_->objects.size())) return;
    const Object& o = p3d_->objects[obj_index];
    if (o.image < 0 || o.image >= texture_count) return;
    const eng::TextureId tex = textures[o.image];
    if (tex == 0) return;
    int frame_no = 0;
    if (action_index >= 0 && action_index < static_cast<int>(o.actions.size())) {
      const Action& a = o.actions[action_index];
      if (!a.frames.empty()) {
        int fi = frame_index;
        if (fi < 0) fi = 0;
        if (fi >= static_cast<int>(a.frames.size())) fi = static_cast<int>(a.frames.size()) - 1;
        frame_no = a.frames[fi].image;
      }
    }
    const int fw = std::max(1, o.frame_w);
    const int fh = std::max(1, o.frame_h);
    const int tex_w = (o.image >= 0 && o.image < static_cast<int>(p3d_->images.size()))
                          ? p3d_->images[o.image].width
                          : fw;
    const int tc = (tex_w / fw > 0) ? frame_no % (tex_w / fw) : 0;
    const int tr = (tex_w / fw > 0) ? frame_no / (tex_w / fw) : 0;
    const eng::TexRegion src = eng::TexRegion::Make(tc * fw, tr * fh, fw, fh);
    dl.DrawImage(eng::Rect2{
                     eng::Vec2{x - static_cast<float>(fw) * 0.5f,
                               y - static_cast<float>(fh) * 0.5f},
                     eng::Vec2{static_cast<float>(fw), static_cast<float>(fh)}},
                 tex, tint, src);
  };

  if (shooter_player_image_ >= 0 && shooter_player_image_ < texture_count &&
      textures[shooter_player_image_] != 0) {
    int fw = 16;
    int fh = 16;
    if (player_object_ >= 0) {
      fw = std::max(1, p3d_->objects[player_object_].frame_w);
      fh = std::max(1, p3d_->objects[player_object_].frame_h);
    }
    dl.DrawImage(eng::Rect2{
                     eng::Vec2{player_.x - static_cast<float>(fw) * 0.5f,
                               player_.y - static_cast<float>(fh) * 0.5f},
                     eng::Vec2{static_cast<float>(fw), static_cast<float>(fh)}},
                 textures[shooter_player_image_], 0xFFFFFFFFu,
                 eng::TexRegion::Make(0, 0, fw, fh));
  }

  for (const ShooterEnemy& e : shooter_enemies_) {
    if (!e.alive) continue;
    if (e.object < 0 || e.object >= static_cast<int>(p3d_->objects.size())) continue;
    const Object& eo = p3d_->objects[e.object];
    int action = eo.default_action;
    if (action < 0 || action >= static_cast<int>(eo.actions.size())) action = 0;
    draw_obj(e.object, e.x, e.y, action, 0, 0xFFFFFFFFu);
  }

  for (const ShooterExplosion& fx : shooter_explosions_) {
    const int frame = std::min(2, static_cast<int>(fx.time * 12.0f));
    draw_obj(fx.object, fx.x, fx.y, 0, frame, 0xFFFFFFFFu);
  }

  for (const ShooterBullet& b : shooter_bullets_) {
    if (b.object < 0 || b.object >= static_cast<int>(p3d_->objects.size())) continue;
    const Object& bo = p3d_->objects[b.object];
    int action = bo.default_action;
    if (action < 0 || action >= static_cast<int>(bo.actions.size())) action = 0;
    draw_obj(b.object, b.x, b.y, action, 0,
             b.from_player ? 0xFFFFFFFFu : 0xFFFFB0B0u);
  }

  if (game_over_) {
    UiDrawTextLine(dl, 0, 150, "GAME OVER", 0xFFFF5050u, 1, view_w_);
  }
}

void Game::RenderPlatformer(eng::DrawList& dl, const eng::TextureId* textures,
                            int texture_count) const {
  if (p3d_ == nullptr) return;
  const Scene& s = p3d_->scenes[scene_index_];
  // 用场景实际 tileset 的 tile 尺寸，不能再硬编码 8x8。
  float tw = 8.0f;
  float th = 8.0f;
  for (int ts_index : s.tilesets) {
    if (ts_index < 0 || ts_index >= static_cast<int>(p3d_->tilesets.size())) continue;
    const Tileset& ts = p3d_->tilesets[static_cast<size_t>(ts_index)];
    if (ts.tile_w > 0 && ts.tile_h > 0) {
      tw = static_cast<float>(ts.tile_w);
      th = static_cast<float>(ts.tile_h);
      break;
    }
  }
  dl.Clear(0xFF000000u);

  // ★ v41：一层里的格子可以来自多个图集页（槽位）★
  //   先按可见范围把格子收进列表（记住槽位），再逐槽位取纹理画。
  struct CellDraw {
    int slot;
    int index;
    float x;
    float y;
  };
  auto draw_layers = [&](bool foreground_pass) {
    for (const SceneLayer& layer : s.layers) {
      if (!layer.visible) continue;
      if (layer.foreground != foreground_pass) continue;
      if (s.tilesets.empty()) continue;
      const float cam_x =
          camera_x_ * static_cast<float>(layer.scroll_x) / 100.0f;
      const float cam_y =
          camera_y_ * static_cast<float>(layer.scroll_y) / 100.0f;
      const int view_w = view_w_;
      const int view_h = view_h_;
      const int c0 = static_cast<int>(std::floor(cam_x / tw)) - 1;
      const int c1 = static_cast<int>(std::ceil((cam_x + view_w) / tw)) + 1;
      const int r0 = static_cast<int>(std::floor(cam_y / th)) - 1;
      const int r1 = static_cast<int>(std::ceil((cam_y + view_h) / th)) + 1;
      std::vector<CellDraw> cells;
      cells.reserve(4096);
      for (const SceneChunk& chunk : layer.chunks) {
        const int base_c = chunk.cx * kP3dChunkCells;
        const int base_r = chunk.cy * kP3dChunkCells;
        if (base_c + kP3dChunkCells <= c0 || base_c > c1 ||
            base_r + kP3dChunkCells <= r0 || base_r > r1) {
          continue;
        }
        for (int ly = 0; ly < kP3dChunkCells; ++ly) {
          const int r = base_r + ly;
          if (r < r0 || r > r1) continue;
          for (int lx = 0; lx < kP3dChunkCells; ++lx) {
            const int c = base_c + lx;
            if (c < c0 || c > c1) continue;
            const size_t idx = static_cast<size_t>(ly) * kP3dChunkCells + lx;
            if (idx >= chunk.cells.size() || chunk.cells[idx] < 0) continue;
            const int v = chunk.cells[idx];
            const int slot = r4::P3dCellSlot(v);
            if (slot < 0 || slot >= static_cast<int>(s.tilesets.size())) continue;
            const float cy2 = static_cast<float>(r) * th - cam_y;
            cells.push_back(CellDraw{slot, r4::P3dCellIndex(v),
                                     static_cast<float>(c) * tw - cam_x, cy2});
          }
        }
      }
      for (size_t si = 0; si < s.tilesets.size(); ++si) {
        const int ts_index = s.tilesets[si];
        if (ts_index < 0 ||
            ts_index >= static_cast<int>(p3d_->tilesets.size())) {
          continue;
        }
        const Tileset& ts = p3d_->tilesets[ts_index];
        if (ts.image < 0 || ts.image >= texture_count) continue;
        const eng::TextureId tex = textures[ts.image];
        if (tex == 0) continue;
        const int cols = std::max(1, ts.columns);
        for (const CellDraw& cd : cells) {
          if (cd.slot != static_cast<int>(si)) continue;
          const eng::TexRegion src = eng::TexRegion::Make(
              (cd.index % cols) * ts.tile_w, (cd.index / cols) * ts.tile_h,
              ts.tile_w, ts.tile_h);
          dl.DrawImage(
              eng::Rect2{eng::Vec2{cd.x, cd.y}, eng::Vec2{tw, th}}, tex,
              0xFFFFFFFFu, src);
        }
      }
    }
  };
  draw_layers(false);

  auto draw_object = [&](int obj_index, float x, float y, int action_index,
                          int frame_index, bool flip_x) {
    if (obj_index < 0 || obj_index >= static_cast<int>(p3d_->objects.size())) return;
    const Object& o = p3d_->objects[obj_index];
    if (o.image < 0 || o.image >= texture_count) return;
    const eng::TextureId tex = textures[o.image];
    if (tex == 0) return;
    int frame_no = 0;
    const int fw = std::max(1, o.frame_w);
    const int fh = std::max(1, o.frame_h);
    int origin_x = fw / 2;
    int origin_y = fh / 2;
    const Action* act = nullptr;
    if (action_index >= 0 && action_index < static_cast<int>(o.actions.size())) {
      act = &o.actions[action_index];
      if (!act->frames.empty()) {
        int fi = frame_index;
        if (fi < 0) fi = 0;
        if (fi >= static_cast<int>(act->frames.size())) fi = static_cast<int>(act->frames.size()) - 1;
        frame_no = act->frames[fi].image;
        origin_x = act->frames[fi].origin_x;
        origin_y = act->frames[fi].origin_y;
      }
    }
    const int off_x = act != nullptr ? act->offset_x : 0;
    const int off_y = act != nullptr ? act->offset_y : 0;
    const int tex_w = (o.image >= 0 && o.image < static_cast<int>(p3d_->images.size()))
                          ? p3d_->images[o.image].width
                          : fw;
    const int tc = (tex_w / fw > 0) ? frame_no % (tex_w / fw) : 0;
    const int tr = (tex_w / fw > 0) ? frame_no / (tex_w / fw) : 0;
    const eng::TexRegion src = eng::TexRegion::Make(tc * fw, tr * fh, fw, fh);
    eng::u8 flags = 0;
    if (flip_x) flags = static_cast<eng::u8>(flags ^ eng::kDrawFlipH);
    if (act != nullptr && act->flip_h) {
      flags = static_cast<eng::u8>(flags ^ eng::kDrawFlipH);
    }
    if (act != nullptr && act->flip_v) flags = static_cast<eng::u8>(flags | eng::kDrawFlipV);
    dl.DrawImage(eng::Rect2{
                     eng::Vec2{x - camera_x_ - static_cast<float>(origin_x) +
                                   static_cast<float>(off_x),
                               y - camera_y_ - static_cast<float>(origin_y) +
                                   static_cast<float>(off_y)},
                     eng::Vec2{static_cast<float>(fw), static_cast<float>(fh)}},
                 tex, 0xFFFFFFFFu, src, flags);
  };

  for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
    const SceneObject& so = s.objects[i];
    if (so.object == player_object_) continue;
    if (so.object < 0 || so.object >= static_cast<int>(p3d_->objects.size())) continue;
    bool enemy_seen = false;
    for (const EnemyState& e : enemies_) {
      if (e.scene_object != i) continue;
      enemy_seen = true;
      if (e.dead) break;
      draw_object(e.object, e.x, e.y, e.action, e.frame, e.facing < 0);
      break;
    }
    if (enemy_seen) continue;
    const Object& eo = p3d_->objects[so.object];
    int action = so.action >= 0 ? so.action : eo.default_action;
    int frame = 0;
    if (action >= 0 && action < static_cast<int>(eo.actions.size())) {
      const Action& a = eo.actions[action];
      if (!a.frames.empty()) {
        const float at = so.action >= 0
                             ? static_cast<float>(frame_count_ - so.anim_start) / 60.0f
                             : static_cast<float>(frame_count_) / 60.0f;
        frame = ActionFrameAt(a, at, nullptr);
      }
    }
    draw_object(so.object, static_cast<float>(so.x), static_cast<float>(so.y), action,
                frame, false);
  }
  RenderProjectiles(dl, textures, texture_count);
  if (player_object_ >= 0) {
    draw_object(player_object_, player_.x, player_.y, player_.action, player_.frame,
                player_.facing > 0);
  }

  draw_layers(true);

  if (won_) {
    UiDrawTextLine(dl, 0, 92, "CLEAR!", 0xFFFFFFFFu, 1, view_w_);
  }

}

void Game::Render(eng::DrawList& dl, const eng::TextureId* textures,
                  int texture_count) const {
  // 注意：DrawToast 在 const 方法里不能调用，这里用 const_cast（只为画字，不改状态）
  Game* self = const_cast<Game*>(this);
  if (p3d_ == nullptr) return;
  if (control_ == ControlProfile::kFree2d &&
      camera_profile_ == CameraProfile::kFixed) {
    // 固定镜头俯视关也用场景图层渲染，不再进旧 shooter 星空背景。
    RenderPlatformer(dl, textures, texture_count);
    RenderProjectiles(dl, textures, texture_count);
    self->DrawToast(dl, textures, texture_count);
    return;
  }
  if (control_ == ControlProfile::kFree2d &&
      camera_profile_ == CameraProfile::kAutoScroll) {
    RenderHShooter(dl, textures, texture_count);
    RenderProjectiles(dl, textures, texture_count);
      self->DrawToast(dl, textures, texture_count);
    return;
  }
  RenderPlatformer(dl, textures, texture_count);
  self->DrawToast(dl, textures, texture_count);
}

}  // namespace r4
