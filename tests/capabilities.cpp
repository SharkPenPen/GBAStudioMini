#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/platform/platform.h"
#include "engine/render/font.h"
#include "engine/render/texture.h"
#include "runtime/game.h"
#include "runtime/p3d.h"
#include "runtime/ui.h"

namespace {
int g_failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                      \
    }                                                                    \
  } while (0)

r4::P3d BaseP3d() {
  r4::P3d p;
  p.view_w = 480;
  p.view_h = 272;
  p.initial_scene = 0;

  r4::Object player;
  player.id = "player";
  player.name = "Player";
  player.type = "player";
  player.frame_w = 16;
  player.frame_h = 16;
  player.col_x = -8;
  player.col_y = -8;
  player.col_w = 16;
  player.col_h = 16;
  player.speed = 120;
  player.gravity = 0;
  player.default_action = 0;

  r4::Action idle;
  idle.name = "idle";
  idle.fps = 1;
  idle.loop = true;
  r4::AnimFrame frame;
  frame.image = 0;
  frame.hold = 1;
  frame.origin_x = 8;
  frame.origin_y = 8;
  frame.col_x = -8;
  frame.col_y = -8;
  frame.col_w = 16;
  frame.col_h = 16;
  idle.frames.push_back(frame);
  player.actions.push_back(idle);

  r4::Rule rule;
  rule.name = "move_right";
  rule.enabled = true;
  r4::RuleCond cond;
  cond.type = 1;  // key_down
  cond.key = "right";
  rule.when.push_back(cond);
  r4::RuleEffect effect;
  effect.type = 0;  // move
  effect.x = 120;
  rule.effects.push_back(effect);
  player.rules.push_back(rule);

  p.objects.push_back(player);

  r4::Scene scene;
  scene.id = "test";
  scene.camera_mode = "follow";
  scene.controlled = 0;
  scene.gravity = 0;
  r4::SceneObject inst;
  inst.object = 0;
  inst.x = 100;
  inst.y = 100;
  scene.objects.push_back(inst);
  for (int i = 0; i < 3; ++i) scene.layers.push_back(r4::SceneLayer{});
  p.scenes.push_back(scene);
  return p;
}

eng::InputState EmptyInput() { return eng::InputState{}; }

eng::InputState RightInput() {
  eng::InputState in;
  in.down[static_cast<int>(eng::Key::kRight)] = true;
  return in;
}

void TestMovement() {
  r4::P3d p = BaseP3d();
  r4::Game game;
  std::string err;
  CHECK(game.Init(p, &err));
  const eng::InputState in = RightInput();
  for (int i = 0; i < 60; ++i) game.Update(1.0f / 60.0f, in);
  CHECK(game.player().x > 200.0f);
}

void TestCollisionBlock() {
  r4::P3d p = BaseP3d();
  r4::SceneCollisionChunk chunk;
  chunk.cx = 1;
  chunk.cy = 0;
  chunk.masks.assign(16 * 16, 0);
  chunk.masks[12 * 16 + 4] = 8;  // wall at x=160, y=96..104
  p.scenes[0].collision.push_back(chunk);

  r4::Game game;
  std::string err;
  CHECK(game.Init(p, &err));
  const eng::InputState in = RightInput();
  for (int i = 0; i < 120; ++i) game.Update(1.0f / 60.0f, in);
  CHECK(game.player().x < 153.0f);
  CHECK(game.player().x > 140.0f);
}

void TestTriggerWin() {
  r4::P3d p = BaseP3d();
  r4::Trigger trigger;
  trigger.x = 80;
  trigger.y = 80;
  trigger.w = 80;
  trigger.h = 80;
  trigger.once = true;
  r4::TriggerAction action;
  action.type = 3;  // win
  trigger.actions.push_back(action);
  p.scenes[0].triggers.push_back(trigger);

  r4::Game game;
  std::string err;
  CHECK(game.Init(p, &err));
  const eng::InputState in = EmptyInput();
  game.Update(1.0f / 60.0f, in);
  CHECK(game.won());
}

void TestFixedCameraCenters() {
  r4::P3d p = BaseP3d();
  p.scenes[0].camera_mode = "fixed";
  r4::SceneLayer layer;
  r4::SceneChunk chunk;
  chunk.cx = 0;
  chunk.cy = 0;
  chunk.cells.assign(16 * 16, -1);
  layer.chunks.push_back(chunk);
  p.scenes[0].layers.push_back(layer);

  r4::Game game;
  std::string err;
  CHECK(game.Init(p, &err));
  CHECK(std::fabs(game.camera_x() + 176.0f) < 0.5f);
  CHECK(std::fabs(game.camera_y() + 72.0f) < 0.5f);
}

void TestP3dRoundTrip() {
  r4::P3d p = BaseP3d();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "pocketloom_cap_test.p3d";
  std::filesystem::remove(path);
  std::string err;
  CHECK(r4::P3dSave(path, p, &err));
  r4::P3d q;
  const bool loaded = r4::P3dLoadFile(path, &q, &err);
  if (!loaded) std::printf("load error: %s\n", err.c_str());
  CHECK(loaded);
  CHECK(q.view_w == 480);
  CHECK(q.view_h == 272);
  CHECK(!q.scenes.empty());
  if (!q.scenes.empty()) CHECK(q.scenes[0].id == "test");
  std::filesystem::remove(path);
}

void TestMirrorY() {
  r4::P3d p;
  p.view_h = 272;
  r4::Scene scene;
  r4::SceneObject obj;
  obj.y = 20;
  scene.objects.push_back(obj);
  p.scenes.push_back(scene);
  r4::MirrorSceneYUp(p, 272);
  CHECK(p.scenes[0].objects[0].y == 252);
}

int UiValueCb(void*, const char* name) {
  return std::strcmp(name, "hp") == 0 ? 50 : 0;
}

void TestUiLayout() {
  const r4::UiRect screen = r4::UiLayoutRect(r4::UiLayout::kScreen, 480, 272);
  CHECK(screen.x == 0 && screen.y == 0 && screen.w == 480 && screen.h == 272);
  const r4::UiRect safe = r4::UiLayoutRect(r4::UiLayout::kSafe, 480, 272);
  CHECK(safe.x == 40 && safe.y == 16 && safe.w == 400 && safe.h == 240);
}

void TestUiAnchor() {
  r4::UiElement e;
  e.anchor = r4::UiAnchor::kBottomRight;
  e.w = 100;
  e.h = 50;
  const r4::UiRect r = r4::UiAnchorRect(e, {0, 0, 480, 272}, 0, 0);
  CHECK(r.x == 380 && r.y == 222);
  e.anchor = r4::UiAnchor::kCenter;
  const r4::UiRect c = r4::UiAnchorRect(e, {0, 0, 480, 272}, 0, 0);
  CHECK(c.x == 190 && c.y == 111);
}

void TestUiWrap() {
  CHECK(eng::FontInit());
  const std::vector<std::string> lines =
      r4::UiWrapText("hello world hello", 40, 10, 0);
  CHECK(lines.size() >= 2);
  for (const std::string& line : lines) {
    CHECK(r4::UiTextWidth(line, 10, 0) <= 40 || line.size() <= 1);
  }
}

void TestUiNineSlice() {
  std::vector<unsigned char> px(24 * 24 * 4, 255);
  const eng::TextureId tex = eng::TextureCreate(px.data(), 24, 24);
  CHECK(tex != 0);
  eng::DrawList dl;
  dl.ResetScreenSizes();
  dl.SetScreenSize(480, 272);
  r4::UiScreen screen;
  r4::UiElement panel;
  panel.kind = r4::UiKind::kPanel;
  panel.w = 80;
  panel.h = 80;
  panel.texture = tex;
  panel.slice = r4::UiNineSlice{8, 8, 8, 8, true};
  screen.elements.push_back(panel);
  r4::UiDrawContext ctx;
  ctx.dl = &dl;
  ctx.view_w = 480;
  ctx.view_h = 272;
  r4::UiDrawScreen(screen, ctx);
  CHECK(dl.count() > 9);
  eng::TextureDestroy(tex);
}

void TestUiValueBar() {
  eng::DrawList dl;
  dl.ResetScreenSizes();
  dl.SetScreenSize(480, 272);
  r4::UiScreen screen;
  r4::UiElement bar;
  bar.kind = r4::UiKind::kValueBar;
  bar.w = 100;
  bar.h = 8;
  bar.binding.name = "hp";
  bar.binding.out_max = 100;
  screen.elements.push_back(bar);
  r4::UiDrawContext ctx;
  ctx.dl = &dl;
  ctx.view_w = 480;
  ctx.view_h = 272;
  ctx.value = UiValueCb;
  r4::UiDrawScreen(screen, ctx);
  CHECK(dl.count() >= 2);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("movement...\n"); TestMovement();
  std::printf("collision...\n"); TestCollisionBlock();
  std::printf("trigger...\n"); TestTriggerWin();
  std::printf("camera...\n"); TestFixedCameraCenters();
  std::printf("p3d roundtrip...\n"); TestP3dRoundTrip();
  std::printf("mirror...\n"); TestMirrorY();
  std::printf("ui layout...\n"); TestUiLayout();
  std::printf("ui anchor...\n"); TestUiAnchor();
  std::printf("ui wrap...\n"); TestUiWrap();
  std::printf("ui nine-slice...\n"); TestUiNineSlice();
  std::printf("ui value bar...\n"); TestUiValueBar();
  if (g_failures != 0) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all capability checks passed\n");
  return 0;
}
