#include "pgmmv/gba_export.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "engine/render/font8x8.h"
#include "engine/render/png.h"

namespace pgmmv {
namespace {

struct Rgba {
  uint8_t r = 0, g = 0, b = 0, a = 0;
};

bool LoadPng(const std::filesystem::path& path, int expected_w, int expected_h,
             std::vector<uint8_t>* rgba, int* width, int* height,
             std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "打不开图片：" + path.u8string();
    return false;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  if (data.empty()) {
    if (error != nullptr) *error = "图片为空：" + path.u8string();
    return false;
  }
  const int w = std::max(1, expected_w);
  const int h = std::max(1, expected_h);
  rgba->assign(static_cast<size_t>(w) * h * 4, 0);
  std::vector<uint8_t> scratch(
      static_cast<size_t>(eng::PngScratchBytes(w, h, 4)));
  eng::PngImage img;
  char why[256] = {};
  if (!eng::PngDecode(data.data(), static_cast<int>(data.size()), rgba->data(),
                      static_cast<int>(rgba->size()), scratch.data(),
                      static_cast<int>(scratch.size()), &img, why,
                      sizeof(why))) {
    if (error != nullptr) *error = "PNG 解码失败：" + std::string(why);
    return false;
  }
  *width = img.width;
  *height = img.height;
  return true;
}

}  // namespace

bool ExportGbaScene(const Project& project, const SceneDoc& scene,
                    const std::filesystem::path& out_header, std::string* error) {
  int layer_index[4] = {-1, -1, -1, -1};
  for (int i = 0; i < static_cast<int>(scene.layers.size()); ++i) {
    const int slot = scene.layers[static_cast<size_t>(i)].bg_slot;
    if (slot >= 0 && slot < 4) layer_index[slot] = i;
  }
  if (layer_index[2] < 0) {
    if (error != nullptr) *error = "场景没有主层（BG2）";
    return false;
  }
  int map_w = scene.map_w;
  int map_h = scene.map_h;
  if ((map_w != 32 && map_w != 64) ||
      (map_h != 32 && map_h != 64)) {
    if (error != nullptr) {
      *error = "GBA 地图只支持 32x32 / 64x32 / 32x64 / 64x64，当前 " +
               std::to_string(map_w) + "x" + std::to_string(map_h) +
               "；GBA 地图上限 512x512（64x64 格）";
    }
    return false;
  }

  std::set<std::string> used_tilesets;
  for (int slot = 1; slot <= 3; ++slot) {
    const int li = layer_index[slot];
    if (li < 0 || !scene.layers[static_cast<size_t>(li)].enabled) continue;
    for (int row = 0; row < map_h; ++row) {
      for (int col = 0; col < map_w; ++col) {
        const int cell = scene.TileAt(li, col, row);
        if (cell < 0) continue;
        const int ts_slot = CellSlot(cell);
        if (ts_slot < 0 || ts_slot >= static_cast<int>(scene.tilesets.size())) continue;
        used_tilesets.insert(scene.tilesets[static_cast<size_t>(ts_slot)]);
      }
    }
  }
  if (used_tilesets.empty()) {
    if (error != nullptr) *error = "BG1–BG3 没有图块";
    return false;
  }
  if (used_tilesets.size() > 1) {
    if (error != nullptr) {
      *error = "G1 只支持单图块组，当前用了 " +
               std::to_string(used_tilesets.size()) + " 组";
    }
    return false;
  }
  const std::string tileset_id = *used_tilesets.begin();
  const TilesetDoc* ts = project.FindTileset(tileset_id);
  if (ts == nullptr) {
    if (error != nullptr) *error = "主层没有图块组";
    return false;
  }
  if (ts->tile_w != 8 || ts->tile_h != 8) {
    if (error != nullptr) *error = "G1 只支持 8x8 图块组";
    return false;
  }
  const Asset* asset = project.FindAssetByPath(ts->image);
  if (asset == nullptr) {
    if (error != nullptr) *error = "图块素材缺失：" + ts->image;
    return false;
  }

  std::vector<uint8_t> rgba;
  int img_w = 0;
  int img_h = 0;
  if (!LoadPng(ProjectAssetPath(project, *asset), asset->width, asset->height,
               &rgba, &img_w, &img_h, error)) {
    return false;
  }
  const int cols = std::max(1, img_w / 8);
  const int rows = std::max(1, img_h / 8);
  auto color_at = [&](int x, int y) -> Rgba {
    const size_t p = (static_cast<size_t>(y) * img_w + x) * 4;
    Rgba c;
    c.r = rgba[p + 0];
    c.g = rgba[p + 1];
    c.b = rgba[p + 2];
    c.a = rgba[p + 3];
    return c;
  };
  auto to15 = [](int v) -> uint16_t {
    return static_cast<uint16_t>((v * 31 + 127) / 255);
  };

  struct TileInfo {
    int r = 0;
    int c = 0;
    std::vector<uint32_t> keys;
    std::vector<Rgba> colors;
  };
  // 导出整张图集的所有图块：每个场景的图块序号/调色板分组因此完全一致，
  // 切场景时只需要换地图/对象，不用重灌 VRAM 图块。
  std::vector<TileInfo> infos;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      TileInfo info;
      info.r = r;
      info.c = c;
      std::map<uint32_t, int> seen;
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
          const Rgba px = color_at(c * 8 + x, r * 8 + y);
          if (px.a < 128) continue;
          const uint32_t ck = (static_cast<uint32_t>(px.r) << 16) |
                              (static_cast<uint32_t>(px.g) << 8) |
                              static_cast<uint32_t>(px.b);
          if (seen.find(ck) != seen.end()) continue;
          seen[ck] = 1;
          info.keys.push_back(ck);
          info.colors.push_back(px);
        }
      }
      infos.push_back(std::move(info));
    }
  }
  if (infos.empty()) {
    if (error != nullptr) *error = "图集没有图块";
    return false;
  }

  struct Bank {
    std::vector<uint16_t> pal;  // 16
    std::vector<Rgba> rgb;      // index 0 = black
    std::map<uint32_t, int> cmap;
    std::vector<size_t> tiles;
  };
  std::vector<Bank> banks;
  for (size_t ti = 0; ti < infos.size(); ++ti) {
    const TileInfo& info = infos[ti];
    int chosen = -1;
    for (size_t bi = 0; bi < banks.size(); ++bi) {
      const Bank& b = banks[bi];
      int add = 0;
      for (size_t k = 0; k < info.keys.size(); ++k) {
        if (b.cmap.find(info.keys[k]) == b.cmap.end()) ++add;
      }
      if (static_cast<int>(b.rgb.size()) + add <= 15) {
        chosen = static_cast<int>(bi);
        break;
      }
    }
    if (chosen < 0) {
      if (banks.size() >= 15) {
        if (error != nullptr) *error = "BG 调色板 bank 超过 15 组";
        return false;
      }
      Bank nb;
      nb.cmap[0] = 0;
      nb.rgb.push_back(Rgba{});
      banks.push_back(std::move(nb));
      chosen = static_cast<int>(banks.size()) - 1;
    }
    Bank& b = banks[static_cast<size_t>(chosen)];
    for (size_t k = 0; k < info.keys.size(); ++k) {
      if (b.cmap.find(info.keys[k]) != b.cmap.end()) continue;
      if (b.rgb.size() >= 16) continue;  // 单图块颜色过多：其余走最近色
      b.cmap[info.keys[k]] = static_cast<int>(b.rgb.size());
      b.rgb.push_back(info.colors[k]);
    }
    b.tiles.push_back(ti);
  }
  for (Bank& b : banks) {
    b.pal.assign(16, 0);
    for (size_t i = 1; i < b.rgb.size(); ++i) {
      b.pal[i] = static_cast<uint16_t>(to15(b.rgb[i].r) |
                                       (to15(b.rgb[i].g) << 5) |
                                       (to15(b.rgb[i].b) << 10));
    }
  }

  std::vector<uint8_t> tiles(32, 0);  // tile 0 = blank
  std::map<std::pair<int, int>, int> tile_no;
  std::map<std::pair<int, int>, int> tile_bank;
  auto nearest = [](const Bank& b, const Rgba& c) -> int {
    int best = 1;
    int best_d = 1 << 30;
    for (int i = 1; i < static_cast<int>(b.rgb.size()); ++i) {
      const int dr = static_cast<int>(c.r) - b.rgb[i].r;
      const int dg = static_cast<int>(c.g) - b.rgb[i].g;
      const int db = static_cast<int>(c.b) - b.rgb[i].b;
      const int d = dr * dr + dg * dg + db * db;
      if (d < best_d) { best_d = d; best = i; }
    }
    return best;
  };
  for (size_t bi = 0; bi < banks.size(); ++bi) {
    Bank& b = banks[bi];
    for (size_t k = 0; k < b.tiles.size(); ++k) {
      const TileInfo& info = infos[b.tiles[k]];
      const int no = static_cast<int>(tiles.size() / 32);
      tiles.resize(tiles.size() + 32, 0);
      const std::pair<int, int> key(info.r, info.c);
      tile_no[key] = no;
      tile_bank[key] = static_cast<int>(bi);
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
          const Rgba c = color_at(info.c * 8 + x, info.r * 8 + y);
          int idx = 0;
          if (c.a >= 128) {
            const uint32_t ck = (static_cast<uint32_t>(c.r) << 16) |
                                (static_cast<uint32_t>(c.g) << 8) |
                                static_cast<uint32_t>(c.b);
            auto it = b.cmap.find(ck);
            idx = it == b.cmap.end() ? nearest(b, c) : it->second;
          }
          const size_t o = static_cast<size_t>(no) * 32 +
                           static_cast<size_t>(y) * 4 + x / 2;
          if ((x & 1) == 0) {
            tiles[o] = static_cast<uint8_t>((tiles[o] & 0xF0) | (idx & 0x0F));
          } else {
            tiles[o] = static_cast<uint8_t>((tiles[o] & 0x0F) | ((idx & 0x0F) << 4));
          }
        }
      }
    }
  }
  if (tiles.size() / 32 > 1024) {
    if (error != nullptr) *error = "BG 图块超过 1024";
    return false;
  }

  std::vector<std::vector<uint16_t>> maps(3, std::vector<uint16_t>(map_w * map_h, 0));
  for (int slot = 1; slot <= 3; ++slot) {
    const int li = layer_index[slot];
    if (li < 0 || !scene.layers[static_cast<size_t>(li)].enabled) continue;
    for (int row = 0; row < map_h; ++row) {
      for (int col = 0; col < map_w; ++col) {
        const int cell = scene.TileAt(li, col, row);
        if (cell < 0) continue;
        const int ts_slot = CellSlot(cell);
        if (ts_slot < 0 || ts_slot >= static_cast<int>(scene.tilesets.size())) continue;
        if (scene.tilesets[static_cast<size_t>(ts_slot)] != tileset_id) continue;
        const int index = CellIndex(cell);
        const int r = index / cols;
        const int c = index % cols;
        const std::pair<int, int> key(r, c);
        auto it = tile_no.find(key);
        if (it == tile_no.end()) continue;
        const int bank = tile_bank[key];
        const int gba_row = map_h - 1 - row;
        maps[slot - 1][static_cast<size_t>(gba_row) * map_w + col] =
            static_cast<uint16_t>((bank << 12) | (it->second & 0x3FF));
      }
    }
  }

  // HUD 字体：8x8 ASCII，4bpp，palette bank 15。
  const int font_base = static_cast<int>(tiles.size() / 32);
  const int font_count = 95;
  if (font_base + font_count > 1024) {
    if (error != nullptr) *error = "HUD 字体超出 BG 图块上限";
    return false;
  }
  tiles.resize(tiles.size() + static_cast<size_t>(font_count) * 32, 0);
  for (int g = 0; g < font_count; ++g) {
    for (int y = 0; y < 8; ++y) {
      const uint8_t bits = eng::kFont8x8[g][y];
      for (int x = 0; x < 8; ++x) {
        if ((bits & (1u << x)) == 0) continue;
        const size_t o = static_cast<size_t>(font_base + g) * 32 +
                         static_cast<size_t>(y) * 4 + x / 2;
        if ((x & 1) == 0) {
          tiles[o] = static_cast<uint8_t>((tiles[o] & 0xF0) | 1);
        } else {
          tiles[o] = static_cast<uint8_t>((tiles[o] & 0x0F) | 0x10);
        }
      }
    }
  }

  std::vector<std::vector<uint16_t>> pal_banks(16, std::vector<uint16_t>(16, 0));
  for (size_t bi = 0; bi < banks.size(); ++bi) pal_banks[bi] = banks[bi].pal;
  pal_banks[15][1] = 0x7FFF;  // HUD 白字
  // ---- 玩家出生点 + 碰撞 + 精灵（G1 第二片） ----
  const SceneObject* spawn = nullptr;
  for (const std::string& id : scene.controlled) {
    if (const SceneObject* o = scene.FindSceneObject(id)) {
      spawn = o;
      break;
    }
  }
  if (spawn == nullptr) {
    for (const SceneObject& o : scene.objects) {
      const ObjectDoc* od = project.FindObject(o.object);
      if (od != nullptr && od->type == "player") {
        spawn = &o;
        break;
      }
    }
  }
  const int spawn_x = spawn != nullptr ? spawn->x : 64;
  const int spawn_y = spawn != nullptr ? spawn->y : 80;
  const std::string obj_id = spawn != nullptr ? spawn->object : "";

  std::vector<uint8_t> collision(static_cast<size_t>(map_w) * map_h, 0);
  for (int row = 0; row < map_h; ++row) {
    for (int col = 0; col < map_w; ++col) {
      collision[static_cast<size_t>(row) * map_w + col] =
          static_cast<uint8_t>(scene.collision.MaskAt(col, row) & 0xFF);
    }
  }

  std::vector<uint16_t> obj_palette;
  obj_palette.push_back(0);
  std::vector<uint8_t> obj_tiles;
  int obj_w = 16;
  int obj_h = 16;
  int obj_tile_count = 0;
  int obj_frame_count = 0;
  int player_hp = 100;
  int player_hp_max = 100;
  int player_rule_count = 0;
  std::vector<int> p_cond_n, p_cond_off, p_cond_type, p_cond_key, p_cond_op, p_cond_val;
  std::vector<int> p_fx_n, p_fx_off, p_fx_type, p_fx_x, p_fx_y, p_fx_anim, p_fx_flip;
  int anim_start[4] = {0, 0, 0, 0};
  int anim_len[4] = {1, 1, 1, 1};
  {
    const ObjectDoc* od = project.FindObject(obj_id);
    if (od != nullptr) {
      for (const AttrDef& a : od->attrs) {
        if (a.name == "hp") player_hp = a.value;
        if (a.name == "hp_max") player_hp_max = a.value;
      }
    }
    if (od != nullptr) {
      auto key_bit = [](const std::string& k) -> int {
        if (k == "left") return 1;
        if (k == "right") return 2;
        if (k == "up") return 4;
        if (k == "down") return 8;
        if (k == "jump") return 16;
        if (k == "attack") return 32;
        return 0;
      };
      auto anim_state = [](const std::string& t) -> int {
        if (t == "idle") return 0;
        if (t == "run") return 1;
        if (t == "jump") return 2;
        if (t == "fall") return 3;
        return -1;
      };
      for (const Rule& r : od->rules) {
        if (!r.enabled) continue;
        p_cond_n.push_back(static_cast<int>(r.when.size()));
        p_cond_off.push_back(static_cast<int>(p_cond_type.size()));
        for (const Cond& c : r.when) {
          int t = 0;
          switch (c.type) {
            case RuleCond::kAlways: t = 0; break;
            case RuleCond::kKeyDown: t = 1; break;
            case RuleCond::kKeyPressed: t = 2; break;
            case RuleCond::kKeyUp: t = 3; break;
            case RuleCond::kOnGround: t = 4; break;
            case RuleCond::kOnGroundNot: t = 5; break;
            case RuleCond::kAnimEnd: t = 6; break;
            case RuleCond::kAttrLe: t = 7; break;
            case RuleCond::kVarCmp: t = 8; break;
            case RuleCond::kTimer: t = 9; break;
            case RuleCond::kPlayerDistLt: t = 10; break;
            case RuleCond::kKeyHeld: t = 11; break;
            default: t = 0; break;
          }
          p_cond_type.push_back(t);
          p_cond_key.push_back(key_bit(c.key));
          p_cond_op.push_back(c.op);
          p_cond_val.push_back(c.value);
        }
        p_fx_n.push_back(static_cast<int>(r.effects.size()));
        p_fx_off.push_back(static_cast<int>(p_fx_type.size()));
        for (const Effect& e : r.effects) {
          int t = 0;
          switch (e.type) {
            case RuleAct::kMove: t = 0; break;
            case RuleAct::kMovePx: t = 1; break;
            case RuleAct::kAnim: t = 2; break;
            case RuleAct::kFlip: t = 3; break;
            case RuleAct::kGravity: t = 4; break;
            case RuleAct::kSfx: t = 5; break;
            case RuleAct::kSpawn: t = 6; break;
            case RuleAct::kDestroy: t = 7; break;
            case RuleAct::kAttrAdd: t = 8; break;
            case RuleAct::kAttrSet: t = 9; break;
            case RuleAct::kDamage: t = 10; break;
            case RuleAct::kMoveAdd: t = 11; break;
            case RuleAct::kMoveInput: t = 12; break;
            default: t = 0; break;
          }
          p_fx_type.push_back(t);
          p_fx_x.push_back(e.x);
          p_fx_y.push_back(e.y);
          p_fx_anim.push_back(anim_state(e.text));
          p_fx_flip.push_back(e.text == "left" ? -1 : (e.text == "right" ? 1 : 0));
        }
        ++player_rule_count;
      }
    }

    const AnimationDoc* ad =
        od != nullptr ? FindAnimation(project, od->animation) : nullptr;
    if (od != nullptr && ad != nullptr && !ad->image.empty() &&
        !ad->actions.empty()) {
      const Asset* oa = project.FindAssetByPath(ad->image);
      if (oa != nullptr) {
        std::vector<uint8_t> orgba;
        int ow = 0;
        int oh = 0;
        std::string oerr;
        if (LoadPng(ProjectAssetPath(project, *oa), oa->width, oa->height,
                    &orgba, &ow, &oh, &oerr)) {
          obj_w = ad->frame_w;
          obj_h = ad->frame_h;
          const int fcols = std::max(1, ow / std::max(1, obj_w));
          const int frows = std::max(1, oh / std::max(1, obj_h));
          const int tx = (obj_w + 7) / 8;
          const int ty = (obj_h + 7) / 8;

          std::vector<int> frame_images;
          int starts[4] = {0, 0, 0, 0};
          int lens[4] = {1, 1, 1, 1};
          const int action_n = std::min(4, static_cast<int>(ad->actions.size()));
          for (int ai = 0; ai < action_n; ++ai) {
            const DirectionDef& dir = ad->actions[static_cast<size_t>(ai)];
            starts[ai] = static_cast<int>(frame_images.size());
            for (const ActionFrame& f : dir.frames) frame_images.push_back(f.image);
            lens[ai] = static_cast<int>(dir.frames.size());
            if (lens[ai] < 1) lens[ai] = 1;
          }
          if (frame_images.empty()) frame_images.push_back(0);
          for (int ai = action_n; ai < 4; ++ai) {
            starts[ai] = 0;
            lens[ai] = 1;
          }
          for (int ai = 0; ai < 4; ++ai) {
            if (starts[ai] < 0 || starts[ai] >= static_cast<int>(frame_images.size())) {
              starts[ai] = 0;
            }
            if (starts[ai] + lens[ai] > static_cast<int>(frame_images.size())) {
              lens[ai] = std::max(1, static_cast<int>(frame_images.size()) - starts[ai]);
            }
            anim_start[ai] = starts[ai];
            anim_len[ai] = lens[ai];
          }
          obj_frame_count = static_cast<int>(frame_images.size());

          auto opx = [&](int x, int y) -> Rgba {
            const size_t p = (static_cast<size_t>(y) * ow + x) * 4;
            Rgba c;
            c.r = orgba[p + 0];
            c.g = orgba[p + 1];
            c.b = orgba[p + 2];
            c.a = orgba[p + 3];
            return c;
          };
          std::map<uint32_t, int> ocolor;
          std::vector<Rgba> opal_rgb;
          ocolor[0] = 0;
          opal_rgb.push_back(Rgba{});
          for (int img : frame_images) {
            const int fi = std::max(0, std::min(img, fcols * frows - 1));
            const int fx = (fi % fcols) * obj_w;
            const int fy = (fi / fcols) * obj_h;
            for (int y = 0; y < obj_h; ++y) {
              for (int x = 0; x < obj_w; ++x) {
                const Rgba c = opx(fx + x, fy + y);
                if (c.a < 128) continue;
                const uint32_t key = (static_cast<uint32_t>(c.r) << 16) |
                                     (static_cast<uint32_t>(c.g) << 8) |
                                     static_cast<uint32_t>(c.b);
                if (ocolor.find(key) != ocolor.end()) continue;
                if (obj_palette.size() >= 16) {
                  int best = 1;
                  int best_d = 1 << 30;
                  for (int i = 1; i < static_cast<int>(opal_rgb.size()); ++i) {
                    const int dr = static_cast<int>(c.r) - opal_rgb[i].r;
                    const int dg = static_cast<int>(c.g) - opal_rgb[i].g;
                    const int db = static_cast<int>(c.b) - opal_rgb[i].b;
                    const int d = dr * dr + dg * dg + db * db;
                    if (d < best_d) { best_d = d; best = i; }
                  }
                  ocolor[key] = best;
                  continue;
                }
                ocolor[key] = static_cast<int>(obj_palette.size());
                obj_palette.push_back(static_cast<uint16_t>(
                    to15(c.r) | (to15(c.g) << 5) | (to15(c.b) << 10)));
                opal_rgb.push_back(c);
              }
            }
          }
          obj_palette.resize(16, 0);

          obj_tile_count = obj_frame_count * tx * ty;
          obj_tiles.assign(static_cast<size_t>(obj_tile_count) * 32, 0);
          for (int slot = 0; slot < obj_frame_count; ++slot) {
            const int fi = std::max(0, std::min(frame_images[static_cast<size_t>(slot)],
                                                fcols * frows - 1));
            const int fx = (fi % fcols) * obj_w;
            const int fy = (fi / fcols) * obj_h;
            for (int y = 0; y < obj_h; ++y) {
              for (int x = 0; x < obj_w; ++x) {
                const Rgba c = opx(fx + x, fy + y);
                if (c.a < 128) continue;
                const uint32_t key = (static_cast<uint32_t>(c.r) << 16) |
                                     (static_cast<uint32_t>(c.g) << 8) |
                                     static_cast<uint32_t>(c.b);
                auto itc = ocolor.find(key);
                const int idx = itc == ocolor.end() ? 0 : itc->second;
                if (idx == 0) continue;
                const int tile = slot * tx * ty + (y / 8) * tx + (x / 8);
                const int lx = x % 8;
                const int ly = y % 8;
                const size_t o = static_cast<size_t>(tile) * 32 +
                                 static_cast<size_t>(ly) * 4 + lx / 2;
                if ((lx & 1) == 0) {
                  obj_tiles[o] = static_cast<uint8_t>((obj_tiles[o] & 0xF0) | idx);
                } else {
                  obj_tiles[o] = static_cast<uint8_t>((obj_tiles[o] & 0x0F) | (idx << 4));
                }
              }
            }
          }
        }
      }
    }
  }

  // 场景里的敌人实例：导出第一帧 + 位置；先做静态/巡逻，规则以后再接。
  std::vector<int> enemy_x;
  std::vector<int> enemy_y;
  std::string enemy_obj_id;
  for (const SceneObject& so : scene.objects) {
    const ObjectDoc* od = project.FindObject(so.object);
    if (od == nullptr || od->type != "enemy") continue;
    if (so.x < 0 || so.x >= map_w * 8 || so.y < 0 || so.y >= map_h * 8) continue;
    if (enemy_obj_id.empty()) enemy_obj_id = so.object;
    if (so.object == enemy_obj_id) {
      enemy_x.push_back(so.x);
      enemy_y.push_back(so.y);
    }
  }
  const int enemy_count = static_cast<int>(enemy_x.size());
  std::vector<uint16_t> enemy_palette(16, 0);
  std::vector<uint8_t> enemy_tiles(32, 0);
  int enemy_w = 16;
  int enemy_h = 16;
  int enemy_tile_base = obj_tile_count;
  int enemy_tile_count = 1;
  int enemy_frame_count = 1;
  {
    const ObjectDoc* od = project.FindObject(enemy_obj_id);
    const AnimationDoc* ad =
        od != nullptr ? FindAnimation(project, od->animation) : nullptr;
    if (od != nullptr && ad != nullptr && !ad->image.empty() &&
        !ad->actions.empty() && !ad->actions.front().frames.empty()) {
      const Asset* oa = project.FindAssetByPath(ad->image);
      if (oa != nullptr) {
        std::vector<uint8_t> orgba;
        int ow = 0;
        int oh = 0;
        std::string oerr;
        if (LoadPng(ProjectAssetPath(project, *oa), oa->width, oa->height,
                    &orgba, &ow, &oh, &oerr)) {
          enemy_w = ad->frame_w;
          enemy_h = ad->frame_h;
          const int fcols = std::max(1, ow / std::max(1, enemy_w));
          const int frows = std::max(1, oh / std::max(1, enemy_h));
          const DirectionDef& edir = ad->actions.front();
          std::vector<int> eimgs;
          for (const ActionFrame& ef : edir.frames) eimgs.push_back(ef.image);
          if (eimgs.empty()) eimgs.push_back(0);
          enemy_frame_count = static_cast<int>(eimgs.size());
          std::map<uint32_t, int> ecolor;
          std::vector<Rgba> epal;
          ecolor[0] = 0;
          epal.push_back(Rgba{});
          auto epx = [&](int x, int y) -> Rgba {
            const size_t p = (static_cast<size_t>(y) * ow + x) * 4;
            Rgba c;
            c.r = orgba[p + 0];
            c.g = orgba[p + 1];
            c.b = orgba[p + 2];
            c.a = orgba[p + 3];
            return c;
          };
          for (int img : eimgs) {
            const int fi = std::max(0, std::min(img, fcols * frows - 1));
            const int fx = (fi % fcols) * enemy_w;
            const int fy = (fi / fcols) * enemy_h;
            for (int y = 0; y < enemy_h; ++y) {
              for (int x = 0; x < enemy_w; ++x) {
                const Rgba c = epx(fx + x, fy + y);
                if (c.a < 128) continue;
                const uint32_t key = (static_cast<uint32_t>(c.r) << 16) |
                                     (static_cast<uint32_t>(c.g) << 8) |
                                     static_cast<uint32_t>(c.b);
                if (ecolor.find(key) != ecolor.end()) continue;
                if (ecolor.size() >= 16) continue;
                ecolor[key] = static_cast<int>(ecolor.size());
                epal.push_back(c);
                enemy_palette[static_cast<size_t>(ecolor[key])] = static_cast<uint16_t>(
                    to15(c.r) | (to15(c.g) << 5) | (to15(c.b) << 10));
              }
            }
          }
          const int tx = (enemy_w + 7) / 8;
          const int ty = (enemy_h + 7) / 8;
          enemy_tile_count = enemy_frame_count * tx * ty;
          enemy_tiles.assign(static_cast<size_t>(enemy_tile_count) * 32, 0);
          for (int slot = 0; slot < enemy_frame_count; ++slot) {
            const int fi = std::max(0, std::min(eimgs[static_cast<size_t>(slot)],
                                               fcols * frows - 1));
            const int fx = (fi % fcols) * enemy_w;
            const int fy = (fi / fcols) * enemy_h;
            for (int y = 0; y < enemy_h; ++y) {
              for (int x = 0; x < enemy_w; ++x) {
                const Rgba c = epx(fx + x, fy + y);
                int idx = 0;
                if (c.a >= 128) {
                  const uint32_t key = (static_cast<uint32_t>(c.r) << 16) |
                                       (static_cast<uint32_t>(c.g) << 8) |
                                       static_cast<uint32_t>(c.b);
                  auto it = ecolor.find(key);
                  idx = it == ecolor.end() ? 0 : it->second;
                }
                if (idx == 0) continue;
                const int tile = slot * tx * ty + (y / 8) * tx + (x / 8);
                const int lx = x % 8;
                const int ly = y % 8;
                const size_t o = static_cast<size_t>(tile) * 32 +
                                 static_cast<size_t>(ly) * 4 + lx / 2;
                if ((lx & 1) == 0) {
                  enemy_tiles[o] = static_cast<uint8_t>((enemy_tiles[o] & 0xF0) | idx);
                } else {
                  enemy_tiles[o] = static_cast<uint8_t>((enemy_tiles[o] & 0x0F) | (idx << 4));
                }
              }
            }
          }
        }
      }
    }
  }

  struct GbaTriggerOut {
    int x = 0, y = 0, w = 0, h = 0;
    int once = 1;
    int type = 0;
    int value = 0;
    int target = -1;
  };
  std::vector<GbaTriggerOut> trigger_out;
  for (const TriggerDef& t : scene.triggers) {
    if (t.actions.empty()) continue;
    if (t.actions.size() > 1) {
      if (error != nullptr) {
        *error = "GBA 触发器只支持 1 个动作：" + t.name;
      }
      return false;
    }
    if (trigger_out.size() >= 16) break;
    GbaTriggerOut o;
    o.x = t.x;
    o.y = t.y;
    o.w = t.w;
    o.h = t.h;
    o.once = t.once ? 1 : 0;
    const TriggerAction& a = t.actions.front();
    if (a.type == "win") o.type = 0;
    else if (a.type == "lose") o.type = 1;
    else if (a.type == "attr_add") o.type = 2;
    else if (a.type == "change_scene") o.type = 3;
    else if (a.type == "set_variable") o.type = 4;
    else if (a.type == "play_sfx") o.type = 5;
    else o.type = 6;
    o.value = a.value;
    o.target = -1;
    for (size_t si = 0; si < project.scenes.size(); ++si) {
      if (project.scenes[si].id == a.target) {
        o.target = static_cast<int>(si);
        break;
      }
    }
    trigger_out.push_back(o);
  }
  const int trigger_count = static_cast<int>(trigger_out.size());

  std::ofstream out(out_header, std::ios::binary);
  if (!out) {
    if (error != nullptr) *error = "写不了输出：" + out_header.u8string();
    return false;
  }
  out << "#pragma once\n#include <stdint.h>\n\n";
  out << "static const int kGbaMapW = " << map_w << ";\n";
  out << "static const int kGbaMapH = " << map_h << ";\n";
  out << "static const uint16_t kGbaPaletteBanks[256] = {\n";
  for (int b = 0; b < 16; ++b) {
    out << "  ";
    for (int i = 0; i < 16; ++i) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "0x%04X", pal_banks[b][i]);
      out << buf;
      if (!(b == 15 && i == 15)) out << ",";
    }
    out << "\n";
  }
  out << "};\n";
  out << "static const int kGbaTileCount = " << tiles.size() / 32 << ";\n";
  out << "static const uint8_t kGbaTiles[" << tiles.size() << "] = {\n";
  for (size_t i = 0; i < tiles.size(); ++i) {
    if (i % 16 == 0) out << "  ";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", tiles[i]);
    out << buf;
    if (i + 1 < tiles.size()) out << ",";
    if (i % 16 == 15) out << "\n";
  }
  out << "};\n";
  out << "static const int kGbaFontTileBase = " << font_base << ";\n";
  out << "static const int kGbaFontCount = " << font_count << ";\n";
  for (int mi = 0; mi < 3; ++mi) {
    out << "static const uint16_t kGbaMapBg" << (mi + 1) << "["
        << map_w * map_h << "] = {\n";
    for (size_t i = 0; i < maps[mi].size(); ++i) {
      if (i % 16 == 0) out << "  ";
      char buf[8];
      std::snprintf(buf, sizeof(buf), "0x%04X", maps[mi][i]);
      out << buf;
      if (i + 1 < maps[mi].size()) out << ",";
      if (i % 16 == 15) out << "\n";
    }
    out << "};\n";
  }
  out << "\n";
  const bool big_map = (map_w == 64 && map_h == 64);
  const int bg1_base = big_map ? 20 : 26;
  const int bg2_base = big_map ? 24 : 28;
  const int bg3_base = big_map ? 28 : 30;
  int size_bits = 0;
  if (map_w == 64 && map_h == 32) size_bits = 1;
  else if (map_w == 32 && map_h == 64) size_bits = 2;
  else if (map_w == 64 && map_h == 64) size_bits = 3;
  const int vofs = 0;  // U2c 静态首屏，不做滚动
  out << "static const int kGbaBg1Base = " << bg1_base << ";\n";
  out << "static const int kGbaBg2Base = " << bg2_base << ";\n";
  out << "static const int kGbaBg3Base = " << bg3_base << ";\n";
  out << "static const int kGbaBgSizeBits = " << size_bits << ";\n";
  out << "static const int kGbaVofs = " << vofs << ";\n";
  for (int slot = 1; slot <= 3; ++slot) {
    const int li = layer_index[slot];
    const int px = (li >= 0) ? scene.layers[static_cast<size_t>(li)].parallax_x : 100;
    const int py = (li >= 0) ? scene.layers[static_cast<size_t>(li)].parallax_y : 100;
    out << "static const int kGbaParallaxBg" << slot << "X = " << px << ";\n";
    out << "static const int kGbaParallaxBg" << slot << "Y = " << py << ";\n";
  }
  out << "static const int kGbaEnemyCount = " << enemy_count << ";\n";
  const int enemy_slots = std::max(1, enemy_count);
  out << "static const int kGbaEnemyX[" << enemy_slots << "] = {";
  for (int i = 0; i < enemy_slots; ++i) {
    out << (i == 0 ? "" : ", ") << (i < enemy_count ? enemy_x[static_cast<size_t>(i)] : 0);
  }
  out << "};\n";
  out << "static const int kGbaEnemyY[" << enemy_slots << "] = {";
  for (int i = 0; i < enemy_slots; ++i) {
    out << (i == 0 ? "" : ", ") << (i < enemy_count ? enemy_y[static_cast<size_t>(i)] : 0);
  }
  out << "};\n";
  out << "static const int kGbaEnemyW = " << enemy_w << ";\n";
  out << "static const int kGbaEnemyH = " << enemy_h << ";\n";
  out << "static const int kGbaEnemyTileBase = " << enemy_tile_base << ";\n";
  out << "static const int kGbaEnemyTileCount = " << enemy_tile_count << ";\n";
  out << "static const int kGbaEnemyFrameCount = " << enemy_frame_count << ";\n";
  out << "static const uint16_t kGbaEnemyPalette[16] = {";
  for (int i = 0; i < 16; ++i) {
    out << (i == 0 ? "" : ", ") << "0x";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", enemy_palette[static_cast<size_t>(i)]);
    out << buf;
  }
  out << "};\n";
  out << "static const uint8_t kGbaEnemyTiles[" << enemy_tiles.size() << "] = {\n";
  for (size_t i = 0; i < enemy_tiles.size(); ++i) {
    if (i % 16 == 0) out << "  ";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", enemy_tiles[i]);
    out << buf;
    if (i + 1 < enemy_tiles.size()) out << ",";
    if (i % 16 == 15) out << "\n";
  }
  out << "};\n";
  out << "static const int kGbaTriggerCount = " << trigger_count << ";\n";
  const int trigger_slots = std::max(1, trigger_count);
  for (int field = 0; field < 5; ++field) {
    const char* name = field == 0 ? "kGbaTriggerX" :
                       field == 1 ? "kGbaTriggerY" :
                       field == 2 ? "kGbaTriggerW" :
                       field == 3 ? "kGbaTriggerH" : "kGbaTriggerOnce";
    out << "static const int " << name << "[" << trigger_slots << "] = {";
    for (int i = 0; i < trigger_slots; ++i) {
      int v = 0;
      if (i < trigger_count) {
        const GbaTriggerOut& t = trigger_out[static_cast<size_t>(i)];
        if (field == 0) v = t.x;
        else if (field == 1) v = t.y;
        else if (field == 2) v = t.w;
        else if (field == 3) v = t.h;
        else v = t.once;
      }
      out << (i == 0 ? "" : ", ") << v;
    }
    out << "};\n";
  }
  out << "static const int kGbaTriggerType[" << trigger_slots << "] = {";
  for (int i = 0; i < trigger_slots; ++i) {
    out << (i == 0 ? "" : ", ")
        << (i < trigger_count ? trigger_out[static_cast<size_t>(i)].type : 0);
  }
  out << "};\n";
  out << "static const int kGbaTriggerValue[" << trigger_slots << "] = {";
  for (int i = 0; i < trigger_slots; ++i) {
    out << (i == 0 ? "" : ", ")
        << (i < trigger_count ? trigger_out[static_cast<size_t>(i)].value : 0);
  }
  out << "};\n";
  out << "static const int kGbaTriggerTarget[" << trigger_slots << "] = {";
  for (int i = 0; i < trigger_slots; ++i) {
    out << (i == 0 ? "" : ", ")
        << (i < trigger_count ? trigger_out[static_cast<size_t>(i)].target : -1);
  }
  out << "};\n";
  out << "static const int kGbaSpawnX = " << spawn_x << ";\n";
  out << "static const int kGbaSpawnY = " << spawn_y << ";\n";
  out << "static const uint8_t kGbaCollision[" << map_w * map_h << "] = {\n";
  for (size_t i = 0; i < collision.size(); ++i) {
    if (i % 16 == 0) out << "  ";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", collision[i]);
    out << buf;
    if (i + 1 < collision.size()) out << ",";
    if (i % 16 == 15) out << "\n";
  }
  out << "};\n\n";
  out << "static const int kGbaPlayerHp = " << player_hp << ";\n";
  out << "static const int kGbaPlayerHpMax = " << player_hp_max << ";\n";
  out << "static const int kGbaObjW = " << obj_w << ";\n";
  out << "static const int kGbaObjH = " << obj_h << ";\n";
  out << "static const int kGbaObjTileCount = " << obj_tile_count << ";\n";
  out << "static const int kGbaObjFrameCount = " << obj_frame_count << ";\n";
  out << "static const int kGbaObjAnimStart[4] = {" << anim_start[0] << ", "
      << anim_start[1] << ", " << anim_start[2] << ", " << anim_start[3] << "};\n";
  out << "static const int kGbaObjAnimLen[4] = {" << anim_len[0] << ", "
      << anim_len[1] << ", " << anim_len[2] << ", " << anim_len[3] << "};\n";
  const int rule_slots = std::max(1, player_rule_count);
  const int cond_slots = std::max(1, static_cast<int>(p_cond_type.size()));
  const int fx_slots = std::max(1, static_cast<int>(p_fx_type.size()));
  out << "static const int kGbaPlayerRuleCount = " << player_rule_count << ";\n";
  auto emit_ints = [&](const char* name, const std::vector<int>& v, int slots) {
    out << "static const int " << name << "[" << slots << "] = {";
    for (int i = 0; i < slots; ++i) {
      out << (i == 0 ? "" : ", ") << (i < static_cast<int>(v.size()) ? v[static_cast<size_t>(i)] : 0);
    }
    out << "};\n";
  };
  emit_ints("kGbaPlayerCondN", p_cond_n, rule_slots);
  emit_ints("kGbaPlayerCondOff", p_cond_off, rule_slots);
  emit_ints("kGbaPlayerFxN", p_fx_n, rule_slots);
  emit_ints("kGbaPlayerFxOff", p_fx_off, rule_slots);
  emit_ints("kGbaPlayerCondType", p_cond_type, cond_slots);
  emit_ints("kGbaPlayerCondKey", p_cond_key, cond_slots);
  emit_ints("kGbaPlayerCondOp", p_cond_op, cond_slots);
  emit_ints("kGbaPlayerCondVal", p_cond_val, cond_slots);
  emit_ints("kGbaPlayerFxType", p_fx_type, fx_slots);
  emit_ints("kGbaPlayerFxX", p_fx_x, fx_slots);
  emit_ints("kGbaPlayerFxY", p_fx_y, fx_slots);
  emit_ints("kGbaPlayerFxAnim", p_fx_anim, fx_slots);
  emit_ints("kGbaPlayerFxFlip", p_fx_flip, fx_slots);

  out << "static const uint16_t kGbaObjPalette[16] = {";
  for (int i = 0; i < 16; ++i) {
    out << (i == 0 ? "" : ", ") << "0x";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", obj_palette[static_cast<size_t>(i)]);
    out << buf;
  }
  out << "};\n";
  const size_t obj_bytes = std::max<size_t>(1, obj_tiles.size());
  out << "static const uint8_t kGbaObjTiles[" << obj_bytes << "] = {\n";
  for (size_t i = 0; i < obj_bytes; ++i) {
    if (i % 16 == 0) out << "  ";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X",
                  i < obj_tiles.size() ? obj_tiles[i] : 0);
    out << buf;
    if (i + 1 < obj_bytes) out << ",";
    if (i % 16 == 15) out << "\n";
  }
  out << "};\n";
  return true;
}

}  // namespace pgmmv
