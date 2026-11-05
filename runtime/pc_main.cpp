// runtime/pc_main.cpp
//
// R4 PC 试玩：真实窗口 + 引擎 DrawList + 软件光栅。
// 只读 game.p3d；不解析 JSON。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

#include "engine/platform/platform.h"
#include "engine/render/draw_list.h"
#include "engine/render/texture.h"
#include "runtime/audio.h"
#include "runtime/game.h"
#include "runtime/p3d.h"

namespace {

void WriteReport(const std::string& path, const r4::Game& game, int frames,
                 const char* p3d_path) {
  if (path.empty()) return;
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return;
  std::fprintf(f, "ok=1\n");
  std::fprintf(f, "frames=%d\n", frames);
  std::fprintf(f, "scene=%s\n", game.scene_id().c_str());
  std::fprintf(f, "transitions=%d\n", game.transition_count());
  std::fprintf(f, "hp=%d\n", game.player().hp);
  std::fprintf(f, "audio=%s\n", r4::AudioBackend());
  std::fprintf(f, "x=%.1f\n", static_cast<double>(game.player().x));
  std::fprintf(f, "y=%.1f\n", static_cast<double>(game.player().y));
  std::fprintf(f, "cam_x=%.1f\n", static_cast<double>(game.camera_x()));
  std::fprintf(f, "cam_y=%.1f\n", static_cast<double>(game.camera_y()));
  std::fprintf(f, "finished=%d\n", game.finished() ? 1 : 0);
  std::fprintf(f, "p3d=%s\n", p3d_path == nullptr ? "" : p3d_path);
  std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
  std::string p3d_path;
  std::string report_path;
  int frames_limit = 0;
  bool auto_run = false;
  int scale = 2;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--p3d") == 0 && i + 1 < argc) {
      p3d_path = argv[++i];
    } else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
      report_path = argv[++i];
    } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      frames_limit = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--auto") == 0) {
      auto_run = true;
    } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
      scale = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::printf("usage: game_pc --p3d FILE [--report FILE] [--frames N] [--auto] [--scale N]\n");
      return 0;
    }
  }
  if (p3d_path.empty()) {
    std::fprintf(stderr, "game_pc: --p3d required\n");
    return 2;
  }

  r4::P3d p3d;
  std::string err;
  if (!r4::P3dLoadFile(p3d_path, &p3d, &err)) {
    std::fprintf(stderr, "game_pc: %s\n", err.c_str());
    return 3;
  }
  // 场景数据是「左下原点、y 向上」，引擎内部是屏幕系；这里翻一次（唯一转换点）。
  r4::MirrorSceneYUp(p3d, p3d.view_h);
  std::fprintf(stderr, "game_pc: mirrored scene y (y-up data -> screen space)\n");

  eng::PlatformConfig cfg;
  cfg.title = "PocketLoom Player";
  cfg.width = p3d.view_w;
  cfg.height = p3d.view_h;
  cfg.window_scale = scale > 0 ? scale : 2;
  if (!eng::Platform().init(cfg)) {
    std::fprintf(stderr, "game_pc: platform init failed\n");
    return 4;
  }
  if (!eng::RenderBackend().init()) {
    std::fprintf(stderr, "game_pc: render backend init failed\n");
    eng::Platform().shutdown();
    return 5;
  }

  if (!r4::AudioInit()) {
    std::fprintf(stderr, "game_pc: audio unavailable (running silent)\n");
  }

  eng::TextureId textures[32] = {};
  const int texture_count =
      static_cast<int>(p3d.images.size()) < 32 ? static_cast<int>(p3d.images.size()) : 32;
  for (int i = 0; i < texture_count; ++i) {
    const r4::Image& img = p3d.images[i];
    textures[i] = eng::TextureCreate(img.rgba.data(), img.width, img.height);
    if (textures[i] == 0) {
      std::fprintf(stderr, "game_pc: texture create failed for %s\n", img.id.c_str());
    }
  }

  r4::Game game;
  {
    const std::filesystem::path save_path =
        std::filesystem::path(p3d_path).parent_path() / "save0.dat";
    game.SetSavePath(save_path.u8string());
  }
  if (!game.Init(p3d, &err)) {
    std::fprintf(stderr, "game_pc: %s\n", err.c_str());
    eng::RenderBackend().shutdown();
    eng::Platform().shutdown();
    return 6;
  }

  eng::DrawList dl;
  dl.ResetScreenSizes();
  dl.SetScreenSize(p3d.view_w, p3d.view_h);

  int frames = 0;
  auto last = std::chrono::steady_clock::now();
  while (!eng::Platform().should_quit()) {
    eng::Platform().begin_frame();
    eng::Platform().pump_events();
    if (eng::Platform().should_quit()) break;

    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt <= 0.0f || dt > 0.05f) dt = 1.0f / 60.0f;

    eng::InputState input = eng::Platform().input();
    if (auto_run) {
      input.down[static_cast<int>(eng::Key::kRight)] = true;
      input.pressed[static_cast<int>(eng::Key::kRight)] = false;
      if ((frames % 90) == 30) {
        input.down[static_cast<int>(eng::Key::kA)] = true;
        input.pressed[static_cast<int>(eng::Key::kA)] = true;
      }
    }
    game.Update(dt, input);

    dl.Reset();
    game.Render(dl, textures, texture_count);
    eng::RenderBackend().submit(dl);
    eng::Platform().end_frame();

    ++frames;
    if (frames_limit > 0 && frames >= frames_limit) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  WriteReport(report_path, game, frames, p3d_path.c_str());

  for (int i = 0; i < texture_count; ++i) {
    if (textures[i] != 0) eng::TextureDestroy(textures[i]);
  }
  r4::AudioShutdown();
  eng::RenderBackend().shutdown();
  eng::Platform().shutdown();
  return 0;
}
