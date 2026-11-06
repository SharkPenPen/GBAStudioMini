// Generates a tiny PC demo p3d: one floor tile + one player sprite + HUD.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "runtime/p3d.h"

namespace {

void Fill(r4::Image* img, int w, int h, unsigned char r, unsigned char g,
          unsigned char b, int checker) {
  img->width = w;
  img->height = h;
  img->rgba.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool alt = checker > 0 && ((x / checker + y / checker) & 1) != 0;
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      img->rgba[i + 0] = static_cast<unsigned char>(alt ? r / 2 : r);
      img->rgba[i + 1] = static_cast<unsigned char>(alt ? g / 2 : g);
      img->rgba[i + 2] = static_cast<unsigned char>(alt ? b / 2 : b);
      img->rgba[i + 3] = 255;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path out =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("samples/demo/demo.p3d");
  std::error_code ec;
  if (!out.parent_path().empty()) {
    std::filesystem::create_directories(out.parent_path(), ec);
  }

  r4::P3d p;
  p.initial_scene = 0;
  p.view_w = 480;
  p.view_h = 272;

  r4::Image floor;
  floor.id = "floor";
  Fill(&floor, 16, 16, 70, 110, 80, 8);
  p.images.push_back(floor);

  r4::Image player;
  player.id = "player";
  Fill(&player, 16, 16, 235, 150, 70, 4);
  p.images.push_back(player);

  r4::Tileset ts;
  ts.id = "floor";
  ts.image = 0;
  ts.tile_w = 16;
  ts.tile_h = 16;
  ts.columns = 1;
  p.tilesets.push_back(ts);

  r4::Object obj;
  obj.id = "player";
  obj.name = "Player";
  obj.type = "player";
  obj.image = 1;
  obj.frame_w = 16;
  obj.frame_h = 16;
  obj.col_x = -8;
  obj.col_y = -16;
  obj.col_w = 16;
  obj.col_h = 16;
  obj.speed = 120;
  obj.gravity = 0;
  obj.default_action = 0;

  r4::Action idle;
  idle.name = "idle";
  idle.fps = 4;
  idle.loop = true;
  r4::AnimFrame frame;
  frame.image = 0;
  frame.hold = 1;
  frame.origin_x = 8;
  frame.origin_y = 16;
  frame.col_x = -8;
  frame.col_y = -16;
  frame.col_w = 16;
  frame.col_h = 16;
  idle.frames.push_back(frame);
  obj.actions.push_back(idle);

  r4::Rule right;
  right.name = "right";
  r4::RuleCond rcond;
  rcond.type = 1;  // key_down
  rcond.key = "right";
  right.when.push_back(rcond);
  r4::RuleEffect rmove;
  rmove.type = 0;  // move
  rmove.x = 120;
  right.effects.push_back(rmove);
  obj.rules.push_back(right);

  r4::Rule left;
  left.name = "left";
  r4::RuleCond lcond;
  lcond.type = 1;
  lcond.key = "left";
  left.when.push_back(lcond);
  r4::RuleEffect lmove;
  lmove.type = 0;
  lmove.x = -120;
  left.effects.push_back(lmove);
  obj.rules.push_back(left);

  p.objects.push_back(obj);

  r4::Scene sc;
  sc.id = "main";
  sc.camera_mode = "fixed";
  sc.controlled = 0;
  sc.gravity = 0;
  sc.tilesets = {0};
  for (int i = 0; i < 4; ++i) sc.layers.push_back(r4::SceneLayer{});
  r4::SceneChunk chunk;
  chunk.cx = 0;
  chunk.cy = 0;
  chunk.cells.assign(16 * 16, 0);
  sc.layers[0].chunks.push_back(chunk);
  r4::SceneObject inst;
  inst.object = 0;
  inst.x = 240;
  inst.y = 136;
  sc.objects.push_back(inst);

  r4::UiScreenData hud;
  hud.id = "hud";
  hud.name = "HUD";
  r4::UiElementData label;
  label.id = "title";
  label.kind = 1;  // text
  label.x = 8;
  label.y = 8;
  label.text = "PocketLoom Demo";
  label.font_size = 12;
  label.color = 0xFFFFFFFFu;
  hud.elements.push_back(label);
  p.ui_screens.push_back(hud);
  sc.hud_ui = 0;

  p.scenes.push_back(sc);

  std::string err;
  if (!r4::P3dSave(out.u8string(), p, &err)) {
    std::fprintf(stderr, "demo: %s\n", err.c_str());
    return 1;
  }
  std::printf("%s\n", out.u8string().c_str());
  return 0;
}
