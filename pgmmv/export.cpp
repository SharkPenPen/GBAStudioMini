// pgmmv/export.cpp
//
// 导出 game.p3d。
//
// 大素材策略：
//   - 编辑器保留原始大 PNG；
//   - 导出时只把「实际用到的图块 / 帧」拷进 <=1024 的 POT 图集；
//   - 场景 tile index、tile 定义、对象动作帧都在导出时重映射。
//   - 没被引用的资源不进入 p3d。
#include "pgmmv/export.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <iterator>
#include <set>
#include <string>
#include <fstream>
#include <vector>

#include "runtime/p3d.h"

namespace pgmmv {
void ExportTriggerActionType(const std::string& type, int* out) {
  if (type == "attr_add") *out = 7;
  else if (type == "spawn") *out = 8;
  else if (type == "dialogue") *out = 5;
  else if (type == "play_sfx") *out = 6;
  else if (type == "set_respawn") *out = 1;
  else if (type == "set_variable") *out = 2;
  else if (type == "win") *out = 3;
  else if (type == "lose") *out = 4;
  else if (type == "open_ui") *out = 9;
  else if (type == "close_ui") *out = 10;
  else if (type == "save") *out = 11;
  else if (type == "load") *out = 12;
  else *out = 0;
}



namespace {

struct LoadedImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> rgba;
};

struct AtlasPack {
  int width = 0;
  int height = 0;
  int columns = 0;
  std::vector<unsigned char> rgba;
  std::map<int, int> remap;  // source index -> packed index
};

int NextPot(int v) {
  int p = 1;
  while (p < v) p <<= 1;
  return p;
}

void Blit(const LoadedImage& src, int sx, int sy, int w, int h,
          std::vector<unsigned char>* dst, int dst_w, int dst_h, int dx, int dy) {
  for (int y = 0; y < h; ++y) {
    const int syy = sy + y;
    const int dyy = dy + y;
    if (syy < 0 || syy >= src.height || dyy < 0 || dyy >= dst_h) continue;
    for (int x = 0; x < w; ++x) {
      const int sxx = sx + x;
      const int dxx = dx + x;
      if (sxx < 0 || sxx >= src.width || dxx < 0 || dxx >= dst_w) continue;
      const size_t si = (static_cast<size_t>(syy) * src.width + sxx) * 4u;
      const size_t di = (static_cast<size_t>(dyy) * dst_w + dxx) * 4u;
      std::memcpy(dst->data() + di, src.rgba.data() + si, 4u);
    }
  }
}

bool LoadSourceImage(const Project& project, const std::string& path,
                     std::map<std::string, LoadedImage>* cache,
                     LoadedImage** out, std::string* error) {
  auto it = cache->find(path);
  if (it != cache->end()) {
    *out = &it->second;
    return true;
  }
  const Asset* asset = project.FindAssetByPath(path);
  if (asset == nullptr || asset->kind != AssetKind::kImage) {
    if (error != nullptr) *error = "导出失败：找不到图像资源 " + path;
    return false;
  }
  LoadedImage img;
  std::string ierr;
  if (!LoadPngRgba(ProjectAssetPath(project, *asset), &img.rgba, &img.width,
                   &img.height, &ierr)) {
    if (error != nullptr) *error = "导出失败：PNG 解码 " + path + "：" + ierr;
    return false;
  }
  if (img.width <= 0 || img.height <= 0) {
    if (error != nullptr) *error = "导出失败：图像尺寸无效 " + path;
    return false;
  }
  auto res = cache->emplace(path, std::move(img));
  *out = &res.first->second;
  return true;
}

bool PackAtlas(const LoadedImage& src, int cell_w, int cell_h,
               int src_offset_x, int src_offset_y,
               const std::vector<int>& source_indices, const std::string& id,
               AtlasPack* out, std::string* error) {
  if (cell_w <= 0 || cell_h <= 0) {
    if (error != nullptr) *error = "导出失败：图块/帧尺寸无效";
    return false;
  }
  if (cell_w > 1024 || cell_h > 1024) {
    if (error != nullptr) {
      *error = "导出失败：单个图块/帧 " + std::to_string(cell_w) + "x" +
               std::to_string(cell_h) + " 超过 1024";
    }
    return false;
  }
  std::vector<int> used;
  for (int v : source_indices) {
    if (v < 0) continue;
    if (std::find(used.begin(), used.end(), v) == used.end()) used.push_back(v);
  }
  if (used.empty()) used.push_back(0);
  std::sort(used.begin(), used.end());

  const int max_cols = std::max(1, 1024 / cell_w);
  const int cols = std::min(static_cast<int>(used.size()), max_cols);
  const int atlas_w = NextPot(cols * cell_w);
  if (atlas_w > 1024) {
    if (error != nullptr) *error = "导出失败：图集宽度超过 1024";
    return false;
  }
  const int pack_cols = std::max(1, atlas_w / cell_w);
  const int rows = (static_cast<int>(used.size()) + pack_cols - 1) / pack_cols;
  const int atlas_h = NextPot(rows * cell_h);
  if (atlas_h > 1024) {
    if (error != nullptr) {
      *error = "导出失败：" + id + " 用到的图块/帧太多，单张 1024 图集放不下（" +
               std::to_string(used.size()) + " 个 " + std::to_string(cell_w) +
               "x" + std::to_string(cell_h) + "）";
    }
    return false;
  }

  out->width = atlas_w;
  out->height = atlas_h;
  out->columns = pack_cols;
  out->rgba.assign(static_cast<size_t>(atlas_w) * atlas_h * 4u, 0);
  out->remap.clear();

  const int src_cols =
      std::max(1, (src.width - src_offset_x + cell_w - 1) / cell_w);
  for (size_t i = 0; i < used.size(); ++i) {
    const int index = used[i];
    const int sc = index % src_cols;
    const int sr = index / src_cols;
    const int dx = (static_cast<int>(i) % pack_cols) * cell_w;
    const int dy = (static_cast<int>(i) / pack_cols) * cell_h;
    Blit(src, src_offset_x + sc * cell_w, src_offset_y + sr * cell_h, cell_w,
         cell_h, &out->rgba, atlas_w, atlas_h, dx, dy);
    out->remap[index] = static_cast<int>(i);
  }
  return true;
}

// ★ 图集分页 ★
//   一页宽 = kAtlasPageSize，高按实际行数取 POT（不超过 kAtlasPageSize）。
//   页内索引 = 行 * columns + 列；columns 固定 = 页宽 / tile_w，运行时不用查表。
//   512 一页留余量。
//   验收时临时改成 64 造超页工程（见 docs/atlas-paging.md），改完记得回 512。
constexpr int kAtlasPageSize = 512;

struct SourceTile {
  std::string tileset;  // TilesetDoc::id
  int index = 0;        // 源图里的图块索引（不是页内索引）
};

// 同一 (tile_w, tile_h) 的源图块，按首次使用顺序排。
struct TileGroup {
  int tile_w = 0;
  int tile_h = 0;
  std::vector<SourceTile> items;
  std::set<std::pair<std::string, int>> seen;
};

// 一页 = 一张 image + 一条 tileset。
struct TilePage {
  int tile_w = 0;
  int tile_h = 0;
  int width = 0;
  int height = 0;
  int columns = 0;
  int image = -1;  // p.images 下标
  std::vector<unsigned char> rgba;
  std::vector<SourceTile> placed;  // placed[i] = 页内索引 i 对应的源图块
};

// 格子 → (图块组 id, 源索引)。
bool ResolveSceneCellList(const std::vector<std::string>& tilesets,
                          int cell, const Project& project, std::string* out_id,
                          int* out_index) {
  if (cell < 0) return false;
  const int slot = CellSlot(cell);
  const int index = CellIndex(cell);
  std::string id;
  if (slot >= 0 && slot < static_cast<int>(tilesets.size())) {
    id = tilesets[slot];
  }
  if (project.FindTileset(id) == nullptr) return false;
  *out_id = std::move(id);
  *out_index = index;
  return true;
}

bool ResolveSceneCell(const SceneDoc& s, int cell, const Project& project,
                      std::string* out_id, int* out_index) {
  return ResolveSceneCellList(s.tilesets, cell, project, out_id, out_index);
}

// 把每组源图块按页切开并栅格化；placement[(组, 索引)] = (页号, 页内索引)。
bool BuildTilePages(const Project& project, const std::vector<TileGroup>& groups,
                    std::map<std::string, LoadedImage>* image_cache,
                    std::vector<TilePage>* pages,
                    std::map<std::pair<std::string, int>, std::pair<int, int>>*
                        placement,
                    std::string* error) {
  for (const TileGroup& g : groups) {
    if (g.items.empty()) continue;
    if (g.tile_w > kAtlasPageSize || g.tile_h > kAtlasPageSize) {
      if (error != nullptr) {
        *error = "导出失败：图块 " + std::to_string(g.tile_w) + "x" +
                 std::to_string(g.tile_h) + " 超过页大小 " +
                 std::to_string(kAtlasPageSize);
      }
      return false;
    }
    const int columns = std::max(1, kAtlasPageSize / g.tile_w);
    const int rows_per_page = std::max(1, kAtlasPageSize / g.tile_h);
    const int capacity = columns * rows_per_page;
    const int total = static_cast<int>(g.items.size());
    for (int start = 0; start < total; start += capacity) {
      const int count = std::min(capacity, total - start);
      const int rows = (count + columns - 1) / columns;
      int height = NextPot(rows * g.tile_h);
      if (height > kAtlasPageSize) height = kAtlasPageSize;

      TilePage page;
      page.tile_w = g.tile_w;
      page.tile_h = g.tile_h;
      page.width = kAtlasPageSize;
      page.height = height;
      page.columns = columns;
      page.rgba.assign(static_cast<size_t>(page.width) * height * 4u, 0);
      page.placed.resize(static_cast<size_t>(count));

      for (int k = 0; k < count; ++k) {
        const SourceTile& st = g.items[static_cast<size_t>(start + k)];
        page.placed[static_cast<size_t>(k)] = st;
        const TilesetDoc* ts = project.FindTileset(st.tileset);
        if (ts == nullptr || ts->image.empty()) continue;
        LoadedImage* src = nullptr;
        if (!LoadSourceImage(project, ts->image, image_cache, &src, error)) {
          return false;
        }
        if (src == nullptr) continue;
        const int src_cols = std::max(1, src->width / g.tile_w);
        const int sx = (st.index % src_cols) * g.tile_w;
        const int sy = (st.index / src_cols) * g.tile_h;
        Blit(*src, sx, sy, g.tile_w, g.tile_h, &page.rgba, page.width,
             page.height, (k % columns) * g.tile_w, (k / columns) * g.tile_h);
      }

      const int page_no = static_cast<int>(pages->size());
      for (int k = 0; k < count; ++k) {
        const SourceTile& st = page.placed[static_cast<size_t>(k)];
        (*placement)[std::make_pair(st.tileset, st.index)] =
            std::make_pair(page_no, k);
      }
      pages->push_back(std::move(page));
    }
  }
  return true;
}

// 对象能用到的动作：默认动作 + 按键映射 + 流程链接里从这些动作出发能走到的
std::set<std::string> ReachableActions(const ObjectDoc& o) {
  std::set<std::string> reach;
  auto add = [&reach](const std::string& n) {
    if (!n.empty()) reach.insert(n);
  };
  add(o.default_action);
  // 运行时还会按名字硬找这两个（受击死亡 / 攻击动作），不一定写在流程里
  add("attack");
  add("death");
  bool grew = true;
  while (grew) {
    grew = false;
    for (const FlowLink& l : o.links) {
      if (reach.count(l.from) != 0 && reach.count(l.to) == 0) {
        reach.insert(l.to);
        grew = true;
      }
    }
  }
  return reach;
}

// 只打包对象"能播到"的动作的帧；到不了的动作不打图
std::vector<int> CollectObjectFrames(const std::vector<ActionDef>& acts,
                                     const std::set<std::string>& reach) {
  std::vector<int> used;
  used.push_back(0);
  for (const ActionDef& a : acts) {
    if (reach.empty() || reach.count(a.name) != 0) {
      for (const ActionFrame& f : a.frames) {
        if (f.image >= 0) used.push_back(f.image);
      }
    }
    // 方向动作总是打包，避免规则选了 action@dir 但图没进 atlas。
    for (const DirectionDef& d : a.directions) {
      for (const ActionFrame& f : d.frames) {
        if (f.image >= 0) used.push_back(f.image);
      }
    }
  }
  if (used.size() == 1) {
    // 一个都没命中（数据不全）：退回全部动作，宁可多打也别打空
    for (const ActionDef& a : acts) {
      for (const ActionFrame& f : a.frames) {
        if (f.image >= 0) used.push_back(f.image);
      }
    }
  }
  return used;
}

int FindAction(const r4::Object& o, const std::string& name) {
  for (size_t i = 0; i < o.actions.size(); ++i) {
    if (o.actions[i].name == name) return static_cast<int>(i);
  }
  return 0;
}

int AnchorFromName(const std::string& a) {
  if (a == "top_center") return 1;
  if (a == "top_right") return 2;
  if (a == "mid_left") return 3;
  if (a == "center") return 4;
  if (a == "mid_right") return 5;
  if (a == "bottom_left") return 6;
  if (a == "bottom_center") return 7;
  if (a == "bottom_right") return 8;
  return 0;
}

r4::UiElementData ConvertUiElement(const UiElementDoc& u,
                                   const Project& project,
                                   std::map<std::string, LoadedImage>* image_cache,
                                   std::map<std::string, int>* ui_images,
                                   r4::P3d* p, std::string* error) {
  r4::UiElementData e;
  e.id = u.id;
  if (u.kind == "text") e.kind = 1;
  else if (u.kind == "image") e.kind = 2;
  else if (u.kind == "value_image") e.kind = 3;
  else if (u.kind == "value_bar") e.kind = 4;
  else if (u.kind == "button") e.kind = 7;
  else if (u.kind == "list") e.kind = 6;
  else e.kind = 0;
  e.anchor = AnchorFromName(u.anchor);
  e.x = u.x;
  e.y = u.y;
  e.w = u.w;
  e.h = u.h;
  e.slice_l = u.slice_l;
  e.slice_r = u.slice_r;
  e.slice_t = u.slice_t;
  e.slice_b = u.slice_b;
  e.slice_tile = u.slice_tile;
  e.image = -1;
  if (!u.image.empty()) {
    auto it = ui_images->find(u.image);
    if (it != ui_images->end()) {
      e.image = it->second;
    } else {
      const Asset* asset = project.FindAsset(u.image);
      if (asset != nullptr) {
        LoadedImage* src = nullptr;
        if (LoadSourceImage(project, asset->path, image_cache, &src, error)) {
          r4::Image img;
          img.id = asset->id;
          img.width = src->width;
          img.height = src->height;
          img.rgba = src->rgba;
          const int index = static_cast<int>(p->images.size());
          p->images.push_back(std::move(img));
          (*ui_images)[u.image] = index;
          e.image = index;
        }
      }
    }
  }
  e.src_x = u.src_x;
  e.src_y = u.src_y;
  e.src_w = u.src_w;
  e.src_h = u.src_h;
  if (e.image >= 0 && e.image < static_cast<int>(p->images.size())) {
    const r4::Image& img = p->images[static_cast<size_t>(e.image)];
    if (e.src_w <= 0) e.src_w = img.width;
    if (e.src_h <= 0) e.src_h = img.height;
  }
  e.frames = u.frames;
  e.frame = u.frame;
  e.text = u.text;
  e.font_size = u.font_size;
  e.color = u.color;
  e.align = u.align;
  e.binding.name = u.value_var;
  e.binding.map = u.map;
  e.binding.in_min = u.in_min;
  e.binding.in_max = u.in_max;
  e.binding.out_min = u.out_min;
  e.binding.out_max = u.out_max;
  e.binding.format = u.format;
  e.visible_var = u.visible_var;
  e.visible_value = u.visible_value;
  e.padding = u.padding;
  e.action = u.action;
  e.action_target = u.action_target;
  e.action_value = u.action_value;
  e.only_if_save = u.only_if_save;
  e.items = u.items;
  for (const UiElementDoc& child : u.children) {
    e.children.push_back(ConvertUiElement(child, project, image_cache, ui_images, p, error));
  }
  return e;
}

}  // namespace

bool ExportP3d(const Project& project, const std::filesystem::path& out_path,
               std::string* error) {
  if (project.scenes.empty()) {
    if (error != nullptr) *error = "导出失败：项目没有场景";
    return false;
  }

  r4::P3d p;
  p.formulas = project.formulas;
  for (const auto& kv : project.variables) p.global_vars.emplace_back(kv.first, kv.second);
  std::map<std::string, LoadedImage> image_cache;
  std::map<std::string, int> ui_image_index;
  std::map<std::string, int> ui_index;
  for (const UiScreenDoc& us : project.ui_screens) {
    r4::UiScreenData screen;
    screen.id = us.id;
    screen.name = us.name;
    screen.modal = us.modal || (us.id == project.title_ui);
    screen.layout = (us.layout == "safe") ? 1 : 0;
    screen.dismiss = us.dismiss;
    screen.dismiss_frames = us.dismiss_frames;
    screen.fade_frames = us.fade_frames;
    screen.fade_out_frames = us.fade_out_frames;
    for (const UiElementDoc& ue : us.elements) {
      screen.elements.push_back(ConvertUiElement(
          ue, project, &image_cache, &ui_image_index, &p, error));
    }
    ui_index[us.id] = static_cast<int>(p.ui_screens.size());
    p.ui_screens.push_back(std::move(screen));
  }
  {
    auto it = ui_index.find(project.persistent_ui);
    p.persistent_ui = it == ui_index.end() ? -1 : it->second;
    auto title = ui_index.find(project.title_ui);
    p.title_ui = title == ui_index.end() ? -1 : title->second;
  }

  // 1) 图块：按 (tile_w, tile_h) 分组，组内只填场景实际用到的块，按页切开。
  //    每页一张 image；tileset 条目在第 3 步生成（顺序 = 页号）。
  std::vector<TileGroup> groups;
  std::map<std::pair<int, int>, size_t> group_of;
  for (const SceneDoc& s : project.scenes) {
    for (const SceneLayer& layer : s.layers) {
      for (const auto& entry : layer.chunks) {
        for (int cell : entry.second.cells) {
          std::string id;
          int index = 0;
          const bool ok = ResolveSceneCell(s, cell, project, &id, &index);
          if (!ok) continue;
          const TilesetDoc* ts = project.FindTileset(id);
          if (ts == nullptr) continue;
          if (ts->tile_w <= 0 || ts->tile_h <= 0) {
            if (error != nullptr) *error = "导出失败：图块组 " + ts->id + " 尺寸无效";
            return false;
          }
          const std::pair<int, int> key(ts->tile_w, ts->tile_h);
          size_t gi = 0;
          auto it = group_of.find(key);
          if (it == group_of.end()) {
            gi = groups.size();
            group_of.emplace(key, gi);
            groups.emplace_back();
            groups.back().tile_w = ts->tile_w;
            groups.back().tile_h = ts->tile_h;
          } else {
            gi = it->second;
          }
          TileGroup& g = groups[gi];
          if (g.seen.insert(std::make_pair(id, index)).second) {
            SourceTile st;
            st.tileset = std::move(id);
            st.index = index;
            g.items.push_back(std::move(st));
          }
        }
      }
    }
  }
  std::vector<TilePage> pages;
  std::map<std::pair<std::string, int>, std::pair<int, int>> placement;
  if (!BuildTilePages(project, groups, &image_cache, &pages, &placement, error)) {
    return false;
  }
  for (TilePage& pg : pages) {
    r4::Image img;
    pg.image = static_cast<int>(p.images.size());
    img.id = "atlas_" + std::to_string(pg.image);
    img.width = pg.width;
    img.height = pg.height;
    img.rgba = std::move(pg.rgba);
    p.images.push_back(std::move(img));
  }

  // 2) 只导出"场景用得到"的对象：场景里摆的 + 它们 spawner 生成的对象（传递闭包）。
  //    没用到的对象连图集都不打，p3d 里也不会出现。
  std::set<std::string> used_objects;
  for (const SceneDoc& s : project.scenes) {
    for (const SceneObject& so : s.objects) {
      if (!so.object.empty()) used_objects.insert(so.object);
    }
  }
  // 射击类场景没有"摆放"玩家/子弹：运行时按类型找第一个 player / bullet，
  // 所以这些也得留下，否则打包后玩家会消失。
  for (const SceneDoc& s : project.scenes) {
    // 非横版（相机固定/自动卷轴）才需要"按类型兜底"打包
    if (s.camera_mode != "fixed" && s.camera_mode != "auto_scroll") continue;
    const char* kinds[3] = {"player", "bullet", "effect"};
    for (int k = 0; k < 3; ++k) {
      for (const ObjectDoc& o : project.objects) {
        if (o.type == kinds[k]) {
          used_objects.insert(o.id);
          break;
        }
      }
    }
  }
  bool grew = true;
  while (grew) {
    grew = false;
    for (const ObjectDoc& o : project.objects) {
      if (used_objects.count(o.id) == 0) continue;
      for (const SpawnerDef& sp : o.spawners) {
        if (!sp.object.empty() && used_objects.count(sp.object) == 0) {
          used_objects.insert(sp.object);
          grew = true;
        }
      }
    }
  }
  std::map<std::string, int> object_index;
  for (const ObjectDoc& o : project.objects) {
    if (used_objects.count(o.id) != 0) {
      object_index[o.id] = static_cast<int>(object_index.size());
    }
  }

  // 3) 对象：只打包动作实际用到的帧。
  for (const ObjectDoc& o : project.objects) {
    if (object_index.count(o.id) == 0) continue;
    r4::Object ro;
    ro.id = o.id;
    ro.name = o.name;
    ro.type = o.type;
    ro.team = o.team;
    ro.image = -1;
    ro.frame_w = std::max(1, o.frame.w);
    ro.frame_h = std::max(1, o.frame.h);
    ro.col_x = o.collision.x;
    ro.col_y = o.collision.y;
    ro.col_w = o.collision.w;
    ro.col_h = o.collision.h;
    ro.hurt_x = o.hurt_box.x;
    ro.hurt_y = o.hurt_box.y;
    ro.hurt_w = o.hurt_box.w;
    ro.hurt_h = o.hurt_box.h;
    ro.attack_x = o.attack_box.x;
    ro.attack_y = o.attack_box.y;
    ro.attack_w = o.attack_box.w;
    ro.attack_h = o.attack_box.h;
    ro.speed = o.physics.speed;
    ro.gravity = o.physics.gravity;
    ro.limit_screen = o.physics.limit_screen ? 1 : 0;
    ro.pushable = 0;
    for (const Rule& r : o.rules) {
      if (!r.enabled) continue;
      for (const Cond& c : r.when) {
        if (c.type == RuleCond::kPushable) {
          ro.pushable = 1;
          break;
        }
      }
      if (ro.pushable != 0) break;
    }
    for (const AttrDef& a : o.attrs) {
      r4::ObjectAttr ra;
      ra.name = a.name;
      ra.value = a.value;
      ro.attrs.push_back(std::move(ra));
    }
    for (const Rule& rl : o.rules) {
      r4::Rule rr;
      rr.name = rl.name;
      rr.enabled = rl.enabled;
      for (const Cond& c : rl.when) {
        r4::RuleCond rc;
        rc.type = static_cast<uint8_t>(c.type);
        rc.key = c.key;
        rc.name = c.name;
        rc.op = c.op;
        rc.value = c.value;
        rr.when.push_back(std::move(rc));
      }
      for (const Effect& ef : rl.effects) {
        r4::RuleEffect re;
        re.type = static_cast<uint8_t>(ef.type);
        re.x = ef.x;
        re.y = ef.y;
        re.text = ef.text;
        re.formula = ef.formula;
        rr.effects.push_back(std::move(re));
      }
      ro.rules.push_back(std::move(rr));
    }

    // ★ 动画资源自己带图集和帧尺寸 ★
    //   引用了动画就用动画的；没引用才退回对象身上的（迁移期数据）。
    static const std::vector<ActionDef> kNoActions;
    const AnimationDoc* anim_doc = FindAnimation(project, o.animation);
    const std::vector<ActionDef>* anim_actions =
        anim_doc != nullptr ? &anim_doc->actions : &kNoActions;
    const std::string anim_image = anim_doc != nullptr && !anim_doc->image.empty()
                                       ? anim_doc->image
                                       : o.image;
    const int anim_fw = anim_doc != nullptr ? anim_doc->frame_w : ro.frame_w;
    const int anim_fh = anim_doc != nullptr ? anim_doc->frame_h : ro.frame_h;
    ro.frame_w = anim_fw;
    ro.frame_h = anim_fh;

    AtlasPack atlas;
    bool has_atlas = false;
    if (!anim_image.empty()) {
      LoadedImage* src = nullptr;
      if (!LoadSourceImage(project, anim_image, &image_cache, &src, error)) return false;
      std::string aerr;
      const std::set<std::string> reach = ReachableActions(o);
      const std::vector<int> packed = CollectObjectFrames(*anim_actions, reach);
      if (!PackAtlas(*src, anim_fw, anim_fh, 0, 0, packed, "object_" + o.id,
                     &atlas, &aerr)) {
        if (error != nullptr) *error = aerr;
        return false;
      }
      r4::Image img;
      img.id = "atlas_object_" + o.id;
      img.width = atlas.width;
      img.height = atlas.height;
      img.rgba = std::move(atlas.rgba);
      ro.image = static_cast<int>(p.images.size());
      p.images.push_back(std::move(img));
      has_atlas = true;
      std::fprintf(stdout, "[pack] %s: %zu frames -> %dx%d\n", o.id.c_str(),
                   packed.size(), atlas.width, atlas.height);
    }

    auto emit_action = [&](const std::string& name, const DirectionDef& ad) {
      r4::Action ra;
      ra.name = name;
      ra.offset_x = ad.offset_x;
      ra.offset_y = ad.offset_y;
      ra.flip_h = ad.flip_h;
      ra.flip_v = ad.flip_v;
      ra.fps = ad.fps;
      ra.loop = ad.loop;
      for (size_t i = 0; i < ad.frames.size(); ++i) {
        const int fi = static_cast<int>(i);
        const ActionFrame& af = ad.frames[i];
        r4::AnimFrame rf;
        if (has_atlas) {
          auto it = atlas.remap.find(af.image);
          rf.image = it == atlas.remap.end() ? 0 : it->second;
        } else {
          rf.image = af.image;
        }
        rf.hold = ActionHoldAt(ad, i);
        ResolveFrameOrigin(ad, fi, &rf.origin_x, &rf.origin_y);
        const CollisionBox* col = ResolveFrameBox(ad, fi, 0, &o.collision);
        if (col != nullptr) {
          rf.col_x = col->x;
          rf.col_y = col->y;
          rf.col_w = col->w;
          rf.col_h = col->h;
        }
        const CollisionBox* atk = ResolveFrameBox(ad, fi, 1, nullptr);
        if (atk != nullptr) {
          rf.attack.x = atk->x;
          rf.attack.y = atk->y;
          rf.attack.w = atk->w;
          rf.attack.h = atk->h;
        }
        for (const AttachPoint& ap : af.attach) {
          r4::AttachPoint out;
          out.name = ap.name;
          out.x = ap.x;
          out.y = ap.y;
          rf.attach.push_back(std::move(out));
        }
        const CollisionBox* hurt = ResolveFrameBox(ad, fi, 2, nullptr);
        if (hurt != nullptr) {
          rf.hurt.x = hurt->x;
          rf.hurt.y = hurt->y;
          rf.hurt.w = hurt->w;
          rf.hurt.h = hurt->h;
        }
        ra.frames.push_back(rf);
      }
      ro.actions.push_back(std::move(ra));
    };
    for (const ActionDef& ad : *anim_actions) {
      emit_action(ad.name, ad);
      // 方向动作导出成独立动作名：action@left / action@right...
      // 这样规则里的“播动画”可以明确选方向，运行时按名字精确查找。
      for (const DirectionDef& d : ad.directions) {
        emit_action(ad.name + "@" + d.name, d);
      }
    }
    for (const SpawnerDef& sp : o.spawners) {
      r4::Spawner rs;
      rs.object = -1;
      auto sit = object_index.find(sp.object);
      if (sit != object_index.end()) rs.object = sit->second;
      if (rs.object < 0) continue;
      rs.offset_x = sp.offset_x;
      rs.offset_y = sp.offset_y;
      rs.vx = sp.vx;
      rs.vy = sp.vy;
      rs.lifetime = sp.lifetime;
      rs.count = sp.count;
      rs.spread = sp.spread;
      rs.inherit_velocity = sp.inherit_velocity;
      rs.destroy_on_hit = sp.destroy_on_hit;
      rs.destroy_with_parent = sp.destroy_with_parent;
      ro.spawners.push_back(rs);
    }
    ro.default_action = FindAction(ro, o.default_action);
    for (const FlowLink& fl : o.links) {
      r4::Link rl;
      rl.from = FindAction(ro, fl.from);
      rl.to = FindAction(ro, fl.to);
      for (const FlowCondition& fc : fl.conditions) {
        r4::Condition rc;
        rc.type = static_cast<int>(fc.type);
        rc.key = fc.key;
        rl.conditions.push_back(std::move(rc));
      }
      ro.links.push_back(std::move(rl));
    }
    p.objects.push_back(std::move(ro));
  }

  // 3) 图块组条目：一页一条（v42 起没有 tiles 表了）。
  for (size_t pi = 0; pi < pages.size(); ++pi) {
    const TilePage& pg = pages[pi];
    r4::Tileset rt;
    rt.id = "atlas_" + std::to_string(pi);
    rt.image = pg.image;
    rt.tile_w = pg.tile_w;
    rt.tile_h = pg.tile_h;
    rt.columns = pg.columns;
    p.tilesets.push_back(std::move(rt));
  }

  // 4) 场景：建页表（槽位 → 页），格子改成 PackCell(槽位, 页内索引)。
  for (const SceneDoc& s : project.scenes) {
    r4::Scene rs;
    rs.id = s.id;
    rs.bgm = s.bgm;
    // 先扫一遍：本场景用到的页，按首次使用顺序给槽位。
    std::map<int, int> page_slot;
    for (const SceneLayer& layer : s.layers) {
      for (const auto& entry : layer.chunks) {
        for (int cell : entry.second.cells) {
          std::string id;
          int index = 0;
          if (!ResolveSceneCell(s, cell, project, &id, &index)) continue;
          auto pl = placement.find(std::make_pair(id, index));
          if (pl == placement.end()) continue;
          page_slot.emplace(pl->second.first,
                            static_cast<int>(page_slot.size()));
        }
      }
    }
    rs.tilesets.assign(page_slot.size(), -1);
    for (const auto& kv : page_slot) rs.tilesets[kv.second] = kv.first;

    auto convert_layers = [&](const std::vector<SceneLayer>& src,
                              std::vector<r4::SceneLayer>* out) {
      for (const SceneLayer& layer : src) {
        r4::SceneLayer rl;
        rl.visible = layer.visible;
        rl.scroll_x = layer.parallax_x;
        rl.scroll_y = layer.parallax_y;
        rl.foreground = (layer.priority <= 1);
          for (const auto& entry : layer.chunks) {
          r4::SceneChunk rc;
          rc.cx = entry.second.cx;
          rc.cy = entry.second.cy;
          rc.cells = entry.second.cells;
          for (int& v : rc.cells) {
            if (v < 0) continue;
            std::string id;
            int index = 0;
            const bool ok = ResolveSceneCell(s, v, project, &id, &index);
            if (!ok) {
              v = -1;
              continue;
            }
            auto pl = placement.find(std::make_pair(id, index));
            if (pl == placement.end()) {
              v = -1;
              continue;
            }
            v = r4::P3dPackCell(page_slot[pl->second.first], pl->second.second);
          }
          rl.chunks.push_back(std::move(rc));
        }
        out->push_back(std::move(rl));
      }
    };
    convert_layers(s.layers, &rs.layers);
    for (const auto& entry : s.collision.chunks) {
      r4::SceneCollisionChunk rc;
      rc.cx = entry.second.cx;
      rc.cy = entry.second.cy;
      rc.masks.resize(entry.second.cells.size());
      for (size_t i = 0; i < entry.second.cells.size(); ++i) {
        rc.masks[i] = static_cast<unsigned char>(entry.second.cells[i] & 0xFF);
      }
      rs.collision.push_back(std::move(rc));
    }
    for (const SceneObject& so : s.objects) {
      r4::SceneObject rso;
      rso.object = -1;
      auto oit = object_index.find(so.object);
      if (oit != object_index.end()) rso.object = oit->second;
      if (rso.object < 0) continue;
      rso.x = so.x;
      rso.y = so.y;
      const int mapped = static_cast<int>(rs.objects.size());
      rs.objects.push_back(rso);
      // controlled 里存的是"对象 id"（object_1），不是实例 id
      if (!s.controlled.empty() &&
          (so.object == s.controlled.front() || so.id == s.controlled.front())) {
        rs.controlled = mapped;
      } else if (s.controlled.empty() && rs.controlled < 0) {
        // 场景没指定受控者时，默认第一个 type=player 的实例（老数据几乎没有 controlled）
        const ObjectDoc* od = project.FindObject(so.object);
        if (od != nullptr && od->type == "player") rs.controlled = mapped;
      }
    }

    rs.gravity = s.gravity;
    rs.camera_mode = s.camera_mode.empty() ? "follow" : s.camera_mode;
    rs.camera_offset_x = s.camera.offset_x;
    rs.camera_offset_y = s.camera.offset_y;
    rs.camera_deadzone_w = s.camera.deadzone_w;
    rs.camera_deadzone_h = s.camera.deadzone_h;
    rs.lives = s.lives;
    rs.scroll_speed = s.scroll_speed;
    rs.scroll_speed_y = s.scroll_speed_y;
    rs.scroll_end_x = s.scroll_end_x;
    rs.scroll_end_y = s.scroll_end_y;
    rs.scroll_next = s.scroll_next;
    rs.win_score = s.win_score;
    rs.variables = s.variables;
    for (const TriggerDef& t : s.triggers) {
      r4::Trigger rt;
      rt.name = t.name;
      rt.color = t.color;
      rt.interaction.enabled = t.interaction.enabled;
      rt.interaction.prompt = t.interaction.prompt;
      rt.interaction.ui_id = t.interaction.ui_id;
      rt.interaction.anchor = t.interaction.anchor;
      rt.interaction.key = t.interaction.key;
      rt.interaction.once = t.interaction.once;
      rt.interaction.require_variable = t.interaction.require_variable;
      rt.interaction.require_value = t.interaction.require_value;
      for (const TriggerAction& a : t.interaction.actions) {
        r4::TriggerAction ra;
        ExportTriggerActionType(a.type, &ra.type);
        ra.target = a.target;
        ra.value = a.value;
        rt.interaction.actions.push_back(std::move(ra));
      }
      for (const StatMod& m : t.mods) {
        r4::TriggerMod rm;
        rm.stat = static_cast<int>(m.id);
        rm.op = static_cast<int>(m.op);
        rm.value = m.value;
        rt.mods.push_back(rm);
      }
      rt.x = t.x;
      rt.y = t.y;
      rt.w = t.w;
      rt.h = t.h;
      rt.once = t.once;
      rt.target_object = -1;
      if (!t.target_object.empty()) {
        auto toit = object_index.find(t.target_object);
        if (toit != object_index.end()) rt.target_object = toit->second;
      }
      rt.require_variable = t.require_variable;
      rt.require_value = t.require_value;
      for (const TriggerAction& a : t.actions) {
        r4::TriggerAction ra;
        ExportTriggerActionType(a.type, &ra.type);
        ra.target = a.target;
        ra.value = a.value;
        rt.actions.push_back(std::move(ra));
      }
      for (const TriggerAction& a : t.exit_actions) {
        r4::TriggerAction ra;
        ExportTriggerActionType(a.type, &ra.type);
        ra.target = a.target;
        ra.value = a.value;
        rt.exit_actions.push_back(std::move(ra));
      }
      for (const TriggerAction& a : t.inside_actions) {
        r4::TriggerAction ra;
        ExportTriggerActionType(a.type, &ra.type);
        ra.target = a.target;
        ra.value = a.value;
        rt.inside_actions.push_back(std::move(ra));
      }
      rt.inside_interval = t.inside_interval;
      for (const TriggerRuleDef& rule : t.rules) {
        r4::TriggerRule rr;
        rr.enabled = rule.enabled;
        rr.mode = rule.mode;
        rr.require_variable = rule.require_variable;
        rr.require_value = rule.require_value;
        for (const TriggerAction& a : rule.actions) {
          r4::TriggerAction ra;
          ExportTriggerActionType(a.type, &ra.type);
          ra.target = a.target;
          ra.value = a.value;
          rr.actions.push_back(std::move(ra));
        }
        rt.rules.push_back(std::move(rr));
      }
      rs.triggers.push_back(std::move(rt));
    }
    if (!s.hud_ui.empty()) {
      auto it = ui_index.find(s.hud_ui);
      rs.hud_ui = it == ui_index.end() ? -1 : it->second;
    }
    p.scenes.push_back(std::move(rs));
  }

  for (const Transition& t : project.transitions) {
    r4::Transition rt;
    rt.from = -1;
    rt.to = -1;
    for (size_t i = 0; i < project.scenes.size(); ++i) {
      if (project.scenes[i].id == t.from) rt.from = static_cast<int>(i);
      if (project.scenes[i].id == t.to) rt.to = static_cast<int>(i);
    }
    if (rt.from < 0 || rt.to < 0) continue;
    rt.type = static_cast<int>(t.type);
    rt.value_int = t.value_int;
    rt.value_string = t.value_string;
    rt.set_variable = t.set_variable;
    rt.set_value = t.set_value;
    p.transitions.push_back(std::move(rt));
  }

  p.initial_scene = 0;
  p.view_w = kScreenWidth;
  p.view_h = kScreenHeight;
  for (size_t i = 0; i < project.scenes.size(); ++i) {
    if (project.scenes[i].id == project.settings.initial_scene) {
      p.initial_scene = static_cast<int>(i);
      break;
    }
  }

  {
    std::set<std::string> audio_ids;
    for (const SceneDoc& sc : project.scenes) {
      if (!sc.bgm.empty()) audio_ids.insert(sc.bgm);
      for (const TriggerDef& t : sc.triggers) {
        auto add_trigger_audio = [&](const std::vector<TriggerAction>& list) {
          for (const TriggerAction& a : list) {
            if (a.type == "play_sfx" && !a.target.empty()) audio_ids.insert(a.target);
          }
        };
        add_trigger_audio(t.actions);
        add_trigger_audio(t.exit_actions);
        add_trigger_audio(t.inside_actions);
        add_trigger_audio(t.interaction.actions);
      }
    }
    for (const ObjectDoc& o : project.objects) {
      for (const Rule& r : o.rules) {
        for (const Effect& e : r.effects) {
          if (e.type == RuleAct::kSfx && !e.text.empty()) audio_ids.insert(e.text);
        }
      }
    }
    for (const std::string& id : audio_ids) {
      const Asset* a = project.FindAsset(id);
      if (a == nullptr) continue;
      std::ifstream f(ProjectAssetPath(project, *a), std::ios::binary);
      if (!f) continue;
      r4::AudioClip clip;
      clip.id = id;
      clip.data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
      p.audio.push_back(std::move(clip));
    }
  }
  return r4::P3dSave(out_path, p, error);
}

}  // namespace pgmmv
