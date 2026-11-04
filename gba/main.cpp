// gba/main.cpp —— G1 最小切片：Mode 0 + BG0 静态场景
//
// 数据来自编辑器：
//   gbaedit --open <project> --export-gba gba/generated.h
// 然后 tools/build-gba.ps1 把 generated.h 和本文件一起编进 ROM。
//
// 这一版只画当前场景主层（layers[1]）的左上 30x20 格（240x160）；
// 滚动、对象、碰撞、规则都还没接。
#include <stdint.h>

#include "scenes.h"

namespace gba {

constexpr int kWidth = 240;
constexpr int kHeight = 160;

}  // namespace gba

extern "C" {
uint32_t gbs_isr_table[16];

// freestanding：编译器会为聚合零初始化发出 memset 调用。
void* memset(void* dst, int c, unsigned int n) {
  unsigned char* p = static_cast<unsigned char*>(dst);
  for (unsigned int i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(c);
  return dst;
}
void* memcpy(void* dst, const void* src, unsigned int n) {
  unsigned char* d = static_cast<unsigned char*>(dst);
  const unsigned char* s = static_cast<const unsigned char*>(src);
  for (unsigned int i = 0; i < n; ++i) d[i] = s[i];
  return dst;
}
}

#define REG_DISPCNT (*(volatile uint16_t*)0x04000000)
#define REG_BG0CNT (*(volatile uint16_t*)0x04000008)
#define REG_BG1CNT (*(volatile uint16_t*)0x0400000A)
#define REG_BG2CNT (*(volatile uint16_t*)0x0400000C)
#define REG_BG3CNT (*(volatile uint16_t*)0x0400000E)
#define REG_BG1HOFS (*(volatile uint16_t*)0x04000014)
#define REG_BG2HOFS (*(volatile uint16_t*)0x04000018)
#define REG_BG3HOFS (*(volatile uint16_t*)0x0400001C)
#define REG_BG1VOFS (*(volatile uint16_t*)0x04000016)
#define REG_BG2VOFS (*(volatile uint16_t*)0x0400001A)
#define REG_BG3VOFS (*(volatile uint16_t*)0x0400001E)
#define REG_VCOUNT (*(volatile uint16_t*)0x04000006)
#define MARKER (*(volatile uint32_t*)0x0203FFF0)
#define TICKS (*(volatile uint32_t*)0x0203FFF4)

__attribute__((noinline)) static void Tick() { TICKS = TICKS + 1; }

extern "C" void AgbMain(void) {
  const GbaSceneData* sc = &kGbaScenes[0];
  uint16_t bg_base[256];
  uint16_t obj_base[32];
  int fade = 16;
  int fade_dir = 0;
  int pending_scene = -1;
  // 16 组 BG 调色板（bank0–14 给图层，bank15 给 HUD）。
  volatile uint16_t* pal = reinterpret_cast<volatile uint16_t*>(0x05000000);
  for (int i = 0; i < 256; ++i) pal[i] = sc->palette[i];

  // VRAM 只保证 16 位访问：按 halfword 写，避免 8 位写被硬件忽略。
  volatile uint16_t* vram = reinterpret_cast<volatile uint16_t*>(0x06000000);
  for (int i = 0; i < sc->tile_count * 16; ++i) {
    vram[i] = static_cast<uint16_t>(sc->tiles[i * 2] |
                                    (sc->tiles[i * 2 + 1] << 8));
  }

  // BG1/2/3 各自一张地图；base 由导出器按地图尺寸算好。
  int map_words = sc->map_w * sc->map_h;
  volatile uint16_t* map1 = reinterpret_cast<volatile uint16_t*>(
      0x06000000 + sc->bg1_base * 0x800);
  volatile uint16_t* map2 = reinterpret_cast<volatile uint16_t*>(
      0x06000000 + sc->bg2_base * 0x800);
  volatile uint16_t* map3 = reinterpret_cast<volatile uint16_t*>(
      0x06000000 + sc->bg3_base * 0x800);
  for (int i = 0; i < map_words; ++i) {
    map1[i] = sc->map1[i];
    map2[i] = sc->map2[i];
    map3[i] = sc->map3[i];
  }

  // BG0 HUD：32x32 固定地图，字体图块 + palette bank 3。
  volatile uint16_t* map0 = reinterpret_cast<volatile uint16_t*>(
      0x06000000 + 19 * 0x800);
  for (int i = 0; i < 1024; ++i) map0[i] = 0;
  int score = 0;
  int hp = sc->hp;
  auto put_text = [&](int x, int y, const char* s) {
    while (*s != '\0' && x < 32) {
      const int c = static_cast<unsigned char>(*s++);
      if (c >= 32 && c < 127) {
        map0[y * 32 + x] = static_cast<uint16_t>(
            (15 << 12) | (sc->font_base + c - 32));
      }
      ++x;
    }
  };
  auto put_num = [&](int x, int y, int v) {
    char buf[8];
    int n = 0;
    if (v <= 0) {
      buf[n++] = '0';
    } else {
      while (v > 0 && n < 8) {
        buf[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
      }
    }
    while (n > 0) {
      --n;
      const int c = static_cast<unsigned char>(buf[n]);
      map0[y * 32 + x] = static_cast<uint16_t>(
          (15 << 12) | (sc->font_base + c - 32));
      ++x;
    }
  };
  auto update_hud = [&]() {
    for (int i = 0; i < 1024; ++i) map0[i] = 0;
    put_text(1, 0, "HP");
    put_num(4, 0, hp);
    put_text(1, 1, "SCORE");
    put_num(7, 1, score);
  };
  update_hud();

  // OBJ：调色板在 0x05000200，图块在 0x06010000（OBJ char block 4）。
  volatile uint16_t* obj_pal = reinterpret_cast<volatile uint16_t*>(0x05000200);
  for (int i = 0; i < 16; ++i) obj_pal[i] = sc->obj_palette[i];
  for (int i = 0; i < 16; ++i) obj_pal[16 + i] = sc->enemy_palette[i];
  volatile uint16_t* obj_vram = reinterpret_cast<volatile uint16_t*>(0x06010000);
  const int obj_bytes = sc->obj_tile_count * 32;
  for (int i = 0; i < obj_bytes / 2; ++i) {
    obj_vram[i] = static_cast<uint16_t>(sc->obj_tiles[i * 2] |
                                        (sc->obj_tiles[i * 2 + 1] << 8));
  }
  const int enemy_base = sc->enemy_tile_base * 16;
  for (int i = 0; i < sc->enemy_tile_count * 16; ++i) {
    obj_vram[enemy_base + i] = static_cast<uint16_t>(
        sc->enemy_tiles[i * 2] | (sc->enemy_tiles[i * 2 + 1] << 8));
  }
  for (int i = 0; i < 256; ++i) bg_base[i] = pal[i];
  for (int i = 0; i < 32; ++i) obj_base[i] = obj_pal[i];
  auto apply_fade = [&](int level) {
    for (int i = 0; i < 240; ++i) {  // bank15 留给 HUD，不跟着淡
      const uint16_t c = bg_base[i];
      const int r = ((c & 31) * level) / 16;
      const int g = (((c >> 5) & 31) * level) / 16;
      const int b = (((c >> 10) & 31) * level) / 16;
      pal[i] = static_cast<uint16_t>(r | (g << 5) | (b << 10));
    }
    for (int i = 0; i < 32; ++i) {
      const uint16_t c = obj_base[i];
      const int r = ((c & 31) * level) / 16;
      const int g = (((c >> 5) & 31) * level) / 16;
      const int b = (((c >> 10) & 31) * level) / 16;
      obj_pal[i] = static_cast<uint16_t>(r | (g << 5) | (b << 10));
    }
  };

  // BG1 前景 priority 1 / BG2 主层 2 / BG3 背景 3；BG0 HUD 暂不启用。
  REG_BG0CNT = 19 << 8;  // priority 0, screen block 19, 32x32（避开 64x64 的 BG2）
  REG_BG1CNT = static_cast<uint16_t>(1 | (sc->bg1_base << 8) |
                                     (sc->bg_size << 14));
  REG_BG2CNT = static_cast<uint16_t>(2 | (sc->bg2_base << 8) |
                                     (sc->bg_size << 14));
  REG_BG3CNT = static_cast<uint16_t>(3 | (sc->bg3_base << 8) |
                                     (sc->bg_size << 14));
  REG_DISPCNT = 0x1F00;  // mode 0 + BG0 + BG1 + BG2 + BG3 + OBJ

  // 16x16 玩家：位置用脚底中心（世界 y 向上），OAM 用屏幕左上角。
  volatile uint16_t* oam = reinterpret_cast<volatile uint16_t*>(0x07000000);
  int px = sc->spawn_x;
  int py = sc->spawn_y;
  int vy = 0;
  int cam_x = 0;
  int cam_y = 0;
  bool ground = false;
  bool prev_a = false;
  int prev_keys = 0;
  bool face_left = false;
  int anim_state = 0;
  int prev_state = -1;
  int anim_tick = 0;
  int invuln = 0;
  bool dead = false;
  int dead_timer = 0;
  int trigger_n = sc->trigger_count;
  if (trigger_n > 16) trigger_n = 16;
  int trigger_done[16] = {};
  int enemy_x[8] = {};
  int enemy_y[8] = {};
  int enemy_dir[8] = {};
  int enemy_alive[8] = {};
  int enemy_anim[8] = {};
  int enemy_n = sc->enemy_count;
  if (enemy_n > 8) enemy_n = 8;
  for (int i = 0; i < enemy_n; ++i) {
    enemy_x[i] = sc->enemy_x[i];
    enemy_y[i] = sc->enemy_y[i];
    enemy_dir[i] = 1;
    enemy_alive[i] = 1;
    enemy_anim[i] = 0;
  }

  auto mask_at = [&](int col, int row) -> int {
    if (col < 0 || col >= sc->map_w || row < 0 || row >= sc->map_h) return 0;
    return sc->collision[row * sc->map_w + col];
  };
  auto put_player = [&](int slot, bool flip) {
    const int sx = px - cam_x - sc->obj_w / 2;
    const int sy = 160 - (py - cam_y) - sc->obj_h;
    oam[0] = static_cast<uint16_t>(sy & 0xFF);
    oam[1] = static_cast<uint16_t>((sx & 0x1FF) | (1 << 14) |
                                   (flip ? (1 << 12) : 0));
    oam[2] = static_cast<uint16_t>((slot * 4) | (2 << 10));
    oam[3] = 0;
  };

  auto load_scene = [&](int target) {
    sc = &kGbaScenes[target];
    for (int k = 0; k < 256; ++k) bg_base[k] = sc->palette[k];
    for (int k = 0; k < sc->tile_count * 16; ++k) {
      vram[k] = static_cast<uint16_t>(sc->tiles[k * 2] |
                                      (sc->tiles[k * 2 + 1] << 8));
    }
    for (int k = 240; k < 256; ++k) pal[k] = sc->palette[k];
    for (int k = 0; k < 16; ++k) obj_base[k] = sc->obj_palette[k];
    for (int k = 0; k < 16; ++k) obj_base[16 + k] = sc->enemy_palette[k];
    for (int k = 0; k < 16; ++k) obj_pal[k] = sc->obj_palette[k];
    for (int k = 0; k < 16; ++k) obj_pal[16 + k] = sc->enemy_palette[k];
    for (int k = 0; k < sc->obj_tile_count * 16; ++k) {
      obj_vram[k] = static_cast<uint16_t>(sc->obj_tiles[k * 2] |
                                          (sc->obj_tiles[k * 2 + 1] << 8));
    }
    const int ebase = sc->enemy_tile_base * 16;
    for (int k = 0; k < sc->enemy_tile_count * 16; ++k) {
      obj_vram[ebase + k] = static_cast<uint16_t>(
          sc->enemy_tiles[k * 2] | (sc->enemy_tiles[k * 2 + 1] << 8));
    }
    REG_BG1CNT = static_cast<uint16_t>(1 | (sc->bg1_base << 8) |
                                       (sc->bg_size << 14));
    REG_BG2CNT = static_cast<uint16_t>(2 | (sc->bg2_base << 8) |
                                       (sc->bg_size << 14));
    REG_BG3CNT = static_cast<uint16_t>(3 | (sc->bg3_base << 8) |
                                       (sc->bg_size << 14));
    for (int k = 0; k < map_words; ++k) {
      map1[k] = sc->map1[k];
      map2[k] = sc->map2[k];
      map3[k] = sc->map3[k];
    }
    map_words = sc->map_w * sc->map_h;
    map1 = reinterpret_cast<volatile uint16_t*>(
        0x06000000 + sc->bg1_base * 0x800);
    map2 = reinterpret_cast<volatile uint16_t*>(
        0x06000000 + sc->bg2_base * 0x800);
    map3 = reinterpret_cast<volatile uint16_t*>(
        0x06000000 + sc->bg3_base * 0x800);
    px = sc->spawn_x;
    py = sc->spawn_y;
    vy = 0;
    cam_x = 0;
    cam_y = 0;
    invuln = 60;
    dead = false;
    prev_state = -1;
    prev_a = false;
    ground = false;
    anim_tick = 0;
    face_left = false;
    enemy_n = sc->enemy_count;
    if (enemy_n > 8) enemy_n = 8;
    for (int k = 0; k < enemy_n; ++k) {
      enemy_x[k] = sc->enemy_x[k];
      enemy_y[k] = sc->enemy_y[k];
      enemy_dir[k] = 1;
      enemy_alive[k] = 1;
    }
    for (int k = enemy_n; k < 8; ++k) {
      volatile uint16_t* eo = oam + (k + 1) * 4;
      eo[0] = 0x0200;
    }
    trigger_n = sc->trigger_count;
    if (trigger_n > 16) trigger_n = 16;
    for (int k = 0; k < 16; ++k) trigger_done[k] = 0;
    apply_fade(0);
    update_hud();
    fade = 0;
    pending_scene = -1;
  };

  MARKER = 0x4D414247u;  // "GBAM"
  for (;;) {
    while (REG_VCOUNT != 160) { }
    const uint16_t keys = *(volatile uint16_t*)0x04000130;  // active low
    const bool left = (keys & (1 << 5)) == 0;
    const bool right = (keys & (1 << 4)) == 0;
    const bool a = (keys & (1 << 0)) == 0;

    if (fade_dir != 0) {
      fade += fade_dir * 2;
      if (fade < 0) fade = 0;
      if (fade > 16) fade = 16;
      apply_fade(fade);
      if (fade == 0 && fade_dir < 0) {
        if (pending_scene >= 0) {
          load_scene(pending_scene);
          fade_dir = 1;
        } else {
          fade_dir = 0;  // Game Over：保持全黑，等倒计时
        }
      } else if (fade == 16) {
        fade_dir = 0;
      }
      Tick();
      while (REG_VCOUNT == 160) { }
      continue;
    }

    if (dead) {
      if (dead_timer > 0) --dead_timer;
      if (dead_timer == 0) {
        dead = false;
        hp = sc->hp;
        score = 0;
        px = sc->spawn_x;
        py = sc->spawn_y;
        vy = 0;
        cam_x = 0;
        cam_y = 0;
        invuln = 60;
        prev_state = -1;
        enemy_n = sc->enemy_count;
        if (enemy_n > 8) enemy_n = 8;
        for (int i = 0; i < enemy_n; ++i) {
          enemy_x[i] = sc->enemy_x[i];
          enemy_y[i] = sc->enemy_y[i];
          enemy_dir[i] = 1;
          enemy_alive[i] = 1;
          enemy_anim[i] = 0;
        }
        for (int i = 0; i < 16; ++i) trigger_done[i] = 0;
        fade = 0;
        fade_dir = 1;
        update_hud();
      }
      Tick();
      while (REG_VCOUNT == 160) { }
      continue;
    }

    int rule_vx = 0;
    bool rule_move = false;
    bool rule_jump = false;
    int rule_anim = -1;
    int rule_flip = 0;
    int km = 0;
    {
      if (left) km |= 1;
      if (right) km |= 2;
      if ((keys & (1 << 6)) == 0) km |= 4;   // up
      if ((keys & (1 << 7)) == 0) km |= 8;   // down
      if (a) km |= 16;
      if ((keys & (1 << 1)) == 0) km |= 32;  // B
      for (int r = 0; r < kGbaPlayerRuleCount; ++r) {
        bool hit = true;
        const int c0 = kGbaPlayerCondOff[r];
        const int cn = kGbaPlayerCondN[r];
        for (int ci = 0; ci < cn; ++ci) {
          const int k = c0 + ci;
          const int t = kGbaPlayerCondType[k];
          const int bit = kGbaPlayerCondKey[k];
          bool ok = true;
          if (t == 0) ok = true;
          else if (t == 1) ok = (km & bit) != 0;
          else if (t == 2) ok = (km & bit) != 0 && (prev_keys & bit) == 0;
          else if (t == 3) ok = (km & bit) == 0 && (prev_keys & bit) != 0;
          else if (t == 4) ok = ground;
          else if (t == 5) ok = !ground;
          else if (t == 7) ok = hp <= kGbaPlayerCondVal[k];
          else if (t == 9) {
            const int v = kGbaPlayerCondVal[k];
            ok = v > 0 && (TICKS % static_cast<uint32_t>(v)) == 0;
          } else ok = false;
          if (!ok) { hit = false; break; }
        }
        if (!hit) continue;
        const int f0 = kGbaPlayerFxOff[r];
        const int fn = kGbaPlayerFxN[r];
        for (int fi = 0; fi < fn; ++fi) {
          const int q = f0 + fi;
          const int t = kGbaPlayerFxType[q];
          if (t == 0) {
            const int fx = kGbaPlayerFxX[q];
            if (fx != 0) {
              rule_vx = fx > 0 ? 2 : -2;
              rule_move = true;
            } else if (kGbaPlayerFxY[q] < 0) {
              rule_jump = true;
            }
          } else if (t == 2) {
            rule_anim = kGbaPlayerFxAnim[q];
          } else if (t == 3) {
            rule_flip = kGbaPlayerFxFlip[q];
          }
        }
      }
    }

    // 水平：一步 2px，先撞左右墙。
    int vx = rule_move ? rule_vx : (right ? 2 : (left ? -2 : 0));
    if (vx > 0) {
      const int col = (px + sc->obj_w / 2) / 8;
      for (int row = py / 8; row <= (py + sc->obj_h - 1) / 8; ++row) {
        if (mask_at(col, row) & 8) {  // 格子左边是墙
          px = col * 8 - sc->obj_w / 2;
          vx = 0;
          break;
        }
      }
    } else if (vx < 0) {
      const int col = (px - sc->obj_w / 2) / 8;
      for (int row = py / 8; row <= (py + sc->obj_h - 1) / 8; ++row) {
        if (mask_at(col, row) & 2) {  // 格子右边是墙
          px = (col + 1) * 8 + sc->obj_w / 2;
          vx = 0;
          break;
        }
      }
    }
    px += vx;
    if (px < sc->obj_w / 2) px = sc->obj_w / 2;
    if (px > sc->map_w * 8 - sc->obj_w / 2) {
      px = sc->map_w * 8 - sc->obj_w / 2;
    }

    // 跳跃只在落地时触发一次。
    if (ground && (rule_jump || (a && !prev_a))) {
      vy = 7;
      ground = false;
    }
    prev_a = a;

    vy -= 1;
    if (vy < -12) vy = -12;
    py += vy;

    // 落地：脚底下方格的 up 位，或单向平台（下落时才接）。
    ground = false;
    if (vy <= 0) {
      const int row = (py - 1) / 8;
      if (row >= 0) {
        for (int col = (px - sc->obj_w / 4) / 8;
             col <= (px + sc->obj_w / 4) / 8; ++col) {
          const int m = mask_at(col, row);
          if ((m & 1) || (m & 0x10)) {
            py = (row + 1) * 8;
            vy = 0;
            ground = true;
            break;
          }
        }
      }
    }
    // 顶头。
    if (vy > 0) {
      const int row = (py + sc->obj_h) / 8;
      for (int col = (px - sc->obj_w / 4) / 8;
           col <= (px + sc->obj_w / 4) / 8; ++col) {
        if (mask_at(col, row) & 4) {
          py = row * 8 - sc->obj_h;
          vy = 0;
          break;
        }
      }
    }
    if (py < 0) {  // 掉出世界：扣 10 HP
      hp -= 10;
      if (hp < 0) hp = 0;
      update_hud();
      if (hp == 0) {
        dead = true;
        dead_timer = 120;
        fade_dir = -1;
        pending_scene = -1;
        oam[0] = 0x0200;
        put_text(5, 7, "GAME OVER");
      } else {
        px = sc->spawn_x;
        py = sc->spawn_y;
        vy = 0;
        ground = false;
      }
    }

    // 相机：跟随玩家，夹在地图内。
    {
      int max_x = sc->map_w * 8 - 240;
      if (max_x < 0) max_x = 0;
      int max_y = sc->map_h * 8 - 160;
      if (max_y < 0) max_y = 0;
      int tx = px - 120;
      int ty = py - 80;
      if (tx < 0) tx = 0;
      if (tx > max_x) tx = max_x;
      if (ty < 0) ty = 0;
      if (ty > max_y) ty = max_y;
      cam_x = tx;
      cam_y = ty;
    }
    {
      const int base_v = sc->map_h * 8 - 160;
      int v1 = base_v - cam_y * sc->par1y / 100;
      int v2 = base_v - cam_y * sc->par2y / 100;
      int v3 = base_v - cam_y * sc->par3y / 100;
      REG_BG1HOFS = static_cast<uint16_t>(cam_x * sc->par1x / 100);
      REG_BG2HOFS = static_cast<uint16_t>(cam_x * sc->par2x / 100);
      REG_BG3HOFS = static_cast<uint16_t>(cam_x * sc->par3x / 100);
      REG_BG1VOFS = static_cast<uint16_t>(v1);
      REG_BG2VOFS = static_cast<uint16_t>(v2);
      REG_BG3VOFS = static_cast<uint16_t>(v3);
    }

    if (rule_anim >= 0) {
      // 规则里 fall 排在 jump 后，会覆盖 jump；上升阶段强制显示 jump。
      anim_state = (rule_anim == 3 && !ground && vy > 0) ? 2 : rule_anim;
    } else if (!ground) {
      anim_state = (vy > 0) ? 2 : 3;
    } else {
      anim_state = (vx != 0) ? 1 : 0;
    }
    if (anim_state != prev_state) {
      prev_state = anim_state;
      anim_tick = 0;
    } else {
      ++anim_tick;
    }
    int len = sc->anim_len[anim_state];
    if (len < 1) len = 1;
    const int local = (anim_tick / 8) % len;
    int slot = sc->anim_start[anim_state] + local;
    if (slot < 0 || slot >= sc->obj_frame_count) slot = 0;
    if (rule_flip < 0) face_left = true;
    else if (rule_flip > 0) face_left = false;
    else if (vx > 0) face_left = false;
    else if (vx < 0) face_left = true;
    if (invuln > 0) --invuln;
    bool scene_switched = false;
    for (int i = 0; i < trigger_n; ++i) {
      if (trigger_done[i]) continue;
      if (px + sc->obj_w / 2 < sc->trig_x[i] ||
          px - sc->obj_w / 2 > sc->trig_x[i] + sc->trig_w[i] ||
          py > sc->trig_y[i] + sc->trig_h[i] ||
          py + sc->obj_h < sc->trig_y[i]) {
        continue;
      }
      if (sc->trig_once[i]) trigger_done[i] = 1;
      const int tt = sc->trig_type[i];
      if (tt == 0) {
        put_text(9, 7, "CLEAR");
      } else if (tt == 1) {
        hp = 0;
        update_hud();
        dead = true;
        dead_timer = 120;
        fade_dir = -1;
        pending_scene = -1;
        oam[0] = 0x0200;
        put_text(5, 7, "GAME OVER");
      } else if (tt == 2) {
        hp += sc->trig_value[i];
        if (hp < 0) hp = 0;
        if (hp > sc->hp_max) hp = sc->hp_max;
        update_hud();
      } else if (tt == 3) {
        const int target = sc->trig_target[i];
        if (target >= 0 && target < 4) {
          pending_scene = target;
          fade_dir = -1;
          scene_switched = true;
          break;
        }
      }
    }
    if (scene_switched) continue;
    for (int i = 0; i < enemy_n; ++i) {
      if (!enemy_alive[i]) {
        volatile uint16_t* eo = oam + (i + 1) * 4;
        eo[0] = 0x0200;  // OAM disable
        continue;
      }
      int nx = enemy_x[i] + enemy_dir[i];
      int col = (enemy_dir[i] > 0 ? nx + sc->enemy_w / 2
                                  : nx - sc->enemy_w / 2) / 8;
      int row = enemy_y[i] / 8;
      const int m = mask_at(col, row);
      if ((enemy_dir[i] > 0 && (m & 8)) || (enemy_dir[i] < 0 && (m & 2))) {
        enemy_dir[i] = -enemy_dir[i];
        nx = enemy_x[i];
      }
      if (nx < sc->enemy_w / 2 || nx > sc->map_w * 8 - sc->enemy_w / 2) {
        enemy_dir[i] = -enemy_dir[i];
        nx = enemy_x[i];
      }
      enemy_x[i] = nx;
      int dx = px - enemy_x[i];
      if (dx < 0) dx = -dx;
      if (dx < (sc->obj_w + sc->enemy_w) / 2 &&
          py < enemy_y[i] + sc->enemy_h &&
          py + sc->obj_h > enemy_y[i]) {
        if (vy < 0 && py >= enemy_y[i] + sc->enemy_h / 2) {
          // 踩头：敌人消失，+10 分，弹起
          enemy_alive[i] = 0;
          score += 10;
          update_hud();
          vy = 6;
          volatile uint16_t* eo = oam + (i + 1) * 4;
          eo[0] = 0x0200;
          continue;
        }
        if (invuln == 0) {
          hp -= 10;
          if (hp < 0) hp = 0;
          invuln = 60;
          update_hud();
          if (hp == 0) {
            dead = true;
            dead_timer = 120;
            fade_dir = -1;
            pending_scene = -1;
            oam[0] = 0x0200;
            put_text(5, 7, "GAME OVER");
          }
        }
      }
      const int esx = enemy_x[i] - cam_x - sc->enemy_w / 2;
      const int esy = 160 - (enemy_y[i] - cam_y) - sc->enemy_h;
      volatile uint16_t* eo = oam + (i + 1) * 4;
      eo[0] = static_cast<uint16_t>(esy & 0xFF);
      eo[1] = static_cast<uint16_t>((esx & 0x1FF) | (1 << 14) |
                                    (enemy_dir[i] < 0 ? (1 << 12) : 0));
      ++enemy_anim[i];
      const int eframe = sc->enemy_frame_count > 0
                             ? (enemy_anim[i] / 8) % sc->enemy_frame_count
                             : 0;
      eo[2] = static_cast<uint16_t>(((sc->enemy_tile_base + eframe * 4) & 0x3FF) |
                                    (1 << 12) | (2 << 10));
      eo[3] = 0;
    }
    put_player(slot, face_left);
    prev_keys = km;
    Tick();
    while (REG_VCOUNT == 160) { }
  }
}
