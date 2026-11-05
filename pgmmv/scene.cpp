// pgmmv/scene.cpp
#include "pgmmv/scene.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace pgmmv {

namespace {

bool ReadFileText(const std::filesystem::path& path, std::string* out,
                  std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "cannot open scene file: " + path.u8string();
    return false;
  }
  f.seekg(0, std::ios::end);
  const std::streamoff n = f.tellg();
  if (n < 0) {
    if (error != nullptr) *error = "cannot size scene file";
    return false;
  }
  f.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(n));
  if (n > 0) {
    f.read(out->data(), n);
    if (!f) {
      if (error != nullptr) *error = "cannot read scene file";
      return false;
    }
  }
  return true;
}

bool WriteFileText(const std::filesystem::path& path, const std::string& text,
                   std::string* error) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (error != nullptr) *error = "cannot write scene file";
    return false;
  }
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(f);
}

bool ToInt(const JsonValue& v, int* out) {
  if (!v.IsNumber()) return false;
  const double d = v.number;
  if (!std::isfinite(d) || d < -2147483648.0 || d > 2147483647.0) return false;
  if (d != std::floor(d)) return false;
  *out = static_cast<int>(d);
  return true;
}

bool ReadInt(const JsonValue& obj, const char* key, int* out, bool* found,
             std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) return true;
  *found = true;
  if (!ToInt(*v, out)) {
    if (error != nullptr) *error = std::string("scene field is not int: ") + key;
    return false;
  }
  return true;
}

bool ReadDouble(const JsonValue& obj, const char* key, double* out,
                bool* found, std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) return true;
  *found = true;
  if (!v->IsNumber()) {
    if (error != nullptr) *error = std::string("scene field is not number: ") + key;
    return false;
  }
  *out = v->number;
  return true;
}

bool ReadString(const JsonValue& obj, const char* key, std::string* out,
                bool* found, std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) return true;
  *found = true;
  if (!v->IsString()) {
    if (error != nullptr) *error = std::string("scene field is not string: ") + key;
    return false;
  }
  *out = v->text;
  return true;
}

bool ReadBool(const JsonValue& obj, const char* key, bool* out, bool* found,
              std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) return true;
  *found = true;
  if (!v->IsBool()) {
    if (error != nullptr) *error = std::string("scene field is not bool: ") + key;
    return false;
  }
  *out = v->boolean;
  return true;
}

}  // namespace

namespace {
int FloorDiv(int v, int d) {
  if (v >= 0) return v / d;
  return -((-v + d - 1) / d);
}
int FloorMod(int v, int d) {
  int r = v % d;
  return r < 0 ? r + d : r;
}
int ChunkIndex(int lx, int ly) { return ly * kSceneChunkCells + lx; }
}

SceneChunk* SceneDoc::FindChunk(int layer, int cx, int cy) {
  if (layer < 0 || layer >= static_cast<int>(layers.size())) return nullptr;
  auto it = layers[layer].chunks.find({cx, cy});
  return it == layers[layer].chunks.end() ? nullptr : &it->second;
}

const SceneChunk* SceneDoc::FindChunk(int layer, int cx, int cy) const {
  if (layer < 0 || layer >= static_cast<int>(layers.size())) return nullptr;
  auto it = layers[layer].chunks.find({cx, cy});
  return it == layers[layer].chunks.end() ? nullptr : &it->second;
}

void SceneDoc::EnsureLayers() {
  static const char* names[kSceneLayerCount] = {"前景 1（对象上）", "前景 2（对象上）",
                                                  "背景 1（对象下）", "背景 2（最底）"};
  std::vector<SceneLayer> old = std::move(layers);
  layers.clear();
  layers.resize(kSceneLayerCount);
  for (int i = 0; i < kSceneLayerCount; ++i) {
    layers[i].name = names[i];
    layers[i].priority = i;
    layers[i].enabled = true;
    if (i < static_cast<int>(old.size())) {
      layers[i] = std::move(old[i]);
      if (layers[i].name.empty()) layers[i].name = names[i];
      if (layers[i].priority < 0 || layers[i].priority >= kSceneLayerCount) {
        layers[i].priority = i;
      }
    }
  }
  if (map_w < 1) map_w = 64;
  if (map_h < 1) map_h = 32;
}

SceneObject* SceneDoc::FindSceneObject(const std::string& instance_id) {
  if (instance_id.empty()) return nullptr;
  for (SceneObject& o : objects) {
    if (o.id == instance_id) return &o;
  }
  return nullptr;
}

const SceneObject* SceneDoc::FindSceneObject(const std::string& instance_id) const {
  if (instance_id.empty()) return nullptr;
  for (const SceneObject& o : objects) {
    if (o.id == instance_id) return &o;
  }
  return nullptr;
}

void SceneDoc::EnsureSceneObjectIds() {
  for (size_t i = 0; i < objects.size(); ++i) {
    if (objects[i].id.empty()) continue;
    for (size_t j = 0; j < i; ++j) {
      if (objects[j].id == objects[i].id) {
        objects[i].id.clear();
        break;
      }
    }
  }
  int next = 1;
  for (SceneObject& o : objects) {
    if (!o.id.empty()) continue;
    for (;;) {
      const std::string candidate = "inst_" + std::to_string(next++);
      bool used = false;
      for (const SceneObject& other : objects) {
        if (other.id == candidate) { used = true; break; }
      }
      if (!used) { o.id = candidate; break; }
    }
  }
}

SceneObject& SceneDoc::AddSceneObject(const std::string& object_id, int x, int y) {
  SceneObject obj;
  obj.object = object_id;
  obj.x = x;
  obj.y = y;
  int next = static_cast<int>(objects.size()) + 1;
  for (;;) {
    const std::string candidate = "inst_" + std::to_string(next++);
    bool used = false;
    for (const SceneObject& other : objects) {
      if (other.id == candidate) { used = true; break; }
    }
    if (!used) { obj.id = candidate; break; }
  }
  objects.push_back(std::move(obj));
  return objects.back();
}

void SceneDoc::SetControlled(const std::string& instance_id) {
  controlled.clear();
  if (instance_id.empty()) return;
  if (FindSceneObject(instance_id) == nullptr) return;
  controlled.push_back(instance_id);
}

int SceneDoc::TileAt(int layer, int col, int row) const {
  if (layer < 0 || layer >= static_cast<int>(layers.size())) return -1;
  const int cx = FloorDiv(col, kSceneChunkCells);
  const int cy = FloorDiv(row, kSceneChunkCells);
  const SceneChunk* c = FindChunk(layer, cx, cy);
  if (c == nullptr || c->cells.size() != kSceneChunkCells * kSceneChunkCells) {
    return -1;
  }
  const int i = ChunkIndex(FloorMod(col, kSceneChunkCells),
                           FloorMod(row, kSceneChunkCells));
  return c->cells[static_cast<size_t>(i)];
}

void SceneDoc::SetTileAt(int layer, int col, int row, int value) {
  if (layer < 0 || layer >= static_cast<int>(layers.size())) return;
  const int cx = FloorDiv(col, kSceneChunkCells);
  const int cy = FloorDiv(row, kSceneChunkCells);
  SceneLayer& l = layers[layer];
  if (value < 0) {
    auto it = l.chunks.find({cx, cy});
    if (it == l.chunks.end()) return;
    if (it->second.cells.size() != kSceneChunkCells * kSceneChunkCells) return;
    const int i = ChunkIndex(FloorMod(col, kSceneChunkCells),
                             FloorMod(row, kSceneChunkCells));
    it->second.cells[static_cast<size_t>(i)] = -1;
    bool empty = true;
    for (int v : it->second.cells) {
      if (v >= 0) { empty = false; break; }
    }
    if (empty) l.chunks.erase(it);
    return;
  }
  auto it = l.chunks.find({cx, cy});
  if (it == l.chunks.end()) {
    SceneChunk c;
    c.cx = cx;
    c.cy = cy;
    c.cells.assign(kSceneChunkCells * kSceneChunkCells, -1);
    it = l.chunks.emplace(std::make_pair(cx, cy), std::move(c)).first;
  }
  if (it->second.cells.size() != kSceneChunkCells * kSceneChunkCells) {
    it->second.cells.assign(kSceneChunkCells * kSceneChunkCells, -1);
  }
  const int i = ChunkIndex(FloorMod(col, kSceneChunkCells),
                           FloorMod(row, kSceneChunkCells));
  it->second.cells[static_cast<size_t>(i)] = value;
}

int SceneCollision::MaskAt(int col, int row) const {
  const int cx = FloorDiv(col, kSceneChunkCells);
  const int cy = FloorDiv(row, kSceneChunkCells);
  auto it = chunks.find({cx, cy});
  if (it == chunks.end() ||
      it->second.cells.size() != kSceneChunkCells * kSceneChunkCells) {
    return 0;
  }
  return it->second.cells[static_cast<size_t>(
      FloorMod(row, kSceneChunkCells) * kSceneChunkCells +
      FloorMod(col, kSceneChunkCells))];
}

void SceneCollision::SetMaskAt(int col, int row, int mask) {
  const int cx = FloorDiv(col, kSceneChunkCells);
  const int cy = FloorDiv(row, kSceneChunkCells);
  auto it = chunks.find({cx, cy});
  if (mask == 0) {
    if (it == chunks.end()) return;
    if (it->second.cells.size() != kSceneChunkCells * kSceneChunkCells) return;
    it->second.cells[static_cast<size_t>(
        FloorMod(row, kSceneChunkCells) * kSceneChunkCells +
        FloorMod(col, kSceneChunkCells))] = 0;
    for (int v : it->second.cells) {
      if (v != 0) return;
    }
    chunks.erase(it);
    return;
  }
  if (it == chunks.end()) {
    SceneChunk c;
    c.cx = cx;
    c.cy = cy;
    c.cells.assign(kSceneChunkCells * kSceneChunkCells, 0);
    it = chunks.emplace(std::make_pair(cx, cy), std::move(c)).first;
  }
  if (it->second.cells.size() != kSceneChunkCells * kSceneChunkCells) {
    it->second.cells.assign(kSceneChunkCells * kSceneChunkCells, 0);
  }
  it->second.cells[static_cast<size_t>(
      FloorMod(row, kSceneChunkCells) * kSceneChunkCells +
      FloorMod(col, kSceneChunkCells))] = mask;
}

bool SceneDoc::ContentBounds(int* min_col, int* min_row, int* max_col,
                             int* max_row) const {
  bool any = false;
  int a = 0, b = 0, c = 0, d = 0;
  for (const SceneLayer& l : layers) {
    for (const auto& entry : l.chunks) {
      const SceneChunk& ch = entry.second;
      for (int ly = 0; ly < kSceneChunkCells; ++ly) {
        for (int lx = 0; lx < kSceneChunkCells; ++lx) {
          const size_t idx = static_cast<size_t>(ly) * kSceneChunkCells + lx;
          if (idx >= ch.cells.size() || ch.cells[idx] < 0) continue;
          const int col = ch.cx * kSceneChunkCells + lx;
          const int row = ch.cy * kSceneChunkCells + ly;
          if (!any) { a = c = col; b = d = row; any = true; }
          else {
            if (col < a) a = col;
            if (col > c) c = col;
            if (row < b) b = row;
            if (row > d) d = row;
          }
        }
      }
    }
  }
  for (const auto& entry : collision.chunks) {
    const SceneChunk& ch = entry.second;
    for (int ly = 0; ly < kSceneChunkCells; ++ly) {
      for (int lx = 0; lx < kSceneChunkCells; ++lx) {
        const size_t idx = static_cast<size_t>(ly) * kSceneChunkCells + lx;
        if (idx >= ch.cells.size() || ch.cells[idx] == 0) continue;
        const int col = ch.cx * kSceneChunkCells + lx;
        const int row = ch.cy * kSceneChunkCells + ly;
        if (!any) { a = c = col; b = d = row; any = true; }
        else {
          if (col < a) a = col;
          if (col > c) c = col;
          if (row < b) b = row;
          if (row > d) d = row;
        }
      }
    }
  }
  if (!any) return false;
  if (min_col) *min_col = a;
  if (min_row) *min_row = b;
  if (max_col) *max_col = c;
  if (max_row) *max_row = d;
  return true;
}

const char* TransitionTypeName(TransitionType t) {
  switch (t) {
    case TransitionType::kExit: return "exit";
    case TransitionType::kSwitch: return "switch";
  }
  return "exit";
}

bool TransitionTypeFromName(const std::string& name, TransitionType* out) {
  if (name == "exit") { *out = TransitionType::kExit; return true; }
  if (name == "switch") { *out = TransitionType::kSwitch; return true; }
  return false;
}

SceneDoc SceneCreateDefault(const std::string& id, const std::string& name,
                            const std::string& tileset) {
  SceneDoc s;
  s.id = id;
  s.name = name.empty() ? id : name;
  s.layers.resize(kSceneLayerCount);
  static const char* names[kSceneLayerCount] = {"前景 1（对象上）", "前景 2（对象上）",
                                                  "背景 1（对象下）", "背景 2（最底）"};
  for (int i = 0; i < kSceneLayerCount; ++i) {
    s.layers[i].name = names[i];
    s.layers[i].visible = true;
    s.layers[i].enabled = true;
    s.layers[i].priority = i;
  }
  if (!tileset.empty()) s.tilesets.push_back(tileset);
  s.camera.follow = "player";
  s.gravity = 900;
  s.camera_mode = "follow";
  return s;
}

bool SceneLoad(const std::filesystem::path& path, SceneDoc* out,
               std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) *error = "SceneLoad: null output";
    return false;
  }
  std::string text;
  if (!ReadFileText(path, &text, error)) return false;
  JsonValue root;
  std::string perr;
  if (!JsonParse(text, &root, &perr)) {
    if (error != nullptr) *error = perr;
    return false;
  }
  if (!root.IsObject()) {
    if (error != nullptr) *error = "scene JSON root must be object";
    return false;
  }
  SceneDoc s;
  s.raw = root;
  const JsonValue* fv = root.Find("format_version");
  if (fv != nullptr) {
    int v = 0;
    if (!ToInt(*fv, &v) || v < 1 || v > kSceneFormatVersion) {
      if (error != nullptr) *error = "scene format_version unsupported";
      return false;
    }
    s.format_version = v;
  }
  bool found = false;
  if (!ReadString(root, "id", &s.id, &found, error)) return false;
  if (s.id.empty()) s.id = path.stem().u8string();
  if (!ReadString(root, "name", &s.name, &found, error)) return false;
  const JsonValue* tsl = root.Find("tilesets");
  if (tsl != nullptr && tsl->IsArray()) {
    for (const JsonValue& tv : tsl->items) {
      if (tv.IsString()) s.tilesets.push_back(tv.text);
    }
  }
  if (s.name.empty()) s.name = s.id;
  auto parse_layers = [&](const JsonValue* arr, const char* what,
                          std::vector<SceneLayer>* out) -> bool {
    if (arr == nullptr) return true;
    if (!arr->IsArray()) {
      if (error != nullptr) *error = std::string("scene.") + what + " must be array";
      return false;
    }
    for (const JsonValue& lv : arr->items) {
      if (!lv.IsObject()) {
        if (error != nullptr) *error = "scene layer must be object";
        return false;
      }
      SceneLayer layer;
      if (!ReadString(lv, "name", &layer.name, &found, error)) return false;
      if (!ReadBool(lv, "visible", &layer.visible, &found, error)) return false;
      if (!ReadBool(lv, "locked", &layer.locked, &found, error)) return false;
      if (!ReadBool(lv, "enabled", &layer.enabled, &found, error)) return false;
      if (!ReadInt(lv, "priority", &layer.priority, &found, error)) return false;
      if (!ReadInt(lv, "parallax_x", &layer.parallax_x, &found, error)) return false;
      if (!ReadInt(lv, "parallax_y", &layer.parallax_y, &found, error)) return false;
      const JsonValue* chunks = lv.Find("chunks");
      if (chunks != nullptr) {
        if (!chunks->IsArray()) {
          if (error != nullptr) *error = "scene layer.chunks must be array";
          return false;
        }
        for (const JsonValue& cv : chunks->items) {
          if (!cv.IsObject()) {
            if (error != nullptr) *error = "scene chunk must be object";
            return false;
          }
          SceneChunk c;
          int v = 0;
          if (!ReadInt(cv, "cx", &v, &found, error) || !found) {
            if (error != nullptr) *error = "scene chunk.cx missing";
            return false;
          }
          c.cx = v;
          if (!ReadInt(cv, "cy", &v, &found, error) || !found) {
            if (error != nullptr) *error = "scene chunk.cy missing";
            return false;
          }
          c.cy = v;
          c.cells.assign(kSceneChunkCells * kSceneChunkCells, -1);
          const JsonValue* cells = cv.Find("cells");
          if (cells != nullptr) {
            if (!cells->IsArray() ||
                cells->items.size() !=
                    static_cast<size_t>(kSceneChunkCells * kSceneChunkCells)) {
              if (error != nullptr) *error = "scene chunk.cells invalid";
              return false;
            }
            for (size_t i = 0; i < cells->items.size(); ++i) {
              int n = -1;
              if (!ToInt(cells->items[i], &n)) n = -1;
              c.cells[i] = n;
            }
          }
          layer.chunks.emplace(std::make_pair(c.cx, c.cy), std::move(c));
        }
      }
      if (out->size() < kSceneLayerCount) out->push_back(std::move(layer));
    }
    return true;
  };
  if (!parse_layers(root.Find("layers"), "layers", &s.layers)) {
    return false;
  }
  if (!ReadInt(root, "map_w", &s.map_w, &found, error)) return false;
  if (!ReadInt(root, "map_h", &s.map_h, &found, error)) return false;
  if (s.map_w < 1 || s.map_w > 64 || s.map_h < 1 || s.map_h > 64) {
    if (error != nullptr) {
      *error = "场景地图最大 512x512（64x64 格），当前 " +
               std::to_string(s.map_w) + "x" + std::to_string(s.map_h) +
               " 格";
    }
    return false;
  }
  const JsonValue* collision = root.Find("collision");
  if (collision != nullptr && collision->IsObject()) {
    const JsonValue* chunks = collision->Find("chunks");
    if (chunks != nullptr) {
      if (!chunks->IsArray()) {
        if (error != nullptr) *error = "scene.collision.chunks must be array";
        return false;
      }
      for (const JsonValue& cv : chunks->items) {
        if (!cv.IsObject()) {
          if (error != nullptr) *error = "scene.collision chunk must be object";
          return false;
        }
        SceneChunk c;
        int v = 0;
        if (!ReadInt(cv, "cx", &v, &found, error) || !found) return false;
        c.cx = v;
        if (!ReadInt(cv, "cy", &v, &found, error) || !found) return false;
        c.cy = v;
        c.cells.assign(kSceneChunkCells * kSceneChunkCells, 0);
        const JsonValue* masks = cv.Find("masks");
        if (masks != nullptr) {
          if (!masks->IsArray() ||
              masks->items.size() !=
                  static_cast<size_t>(kSceneChunkCells * kSceneChunkCells)) {
            if (error != nullptr) *error = "scene.collision chunk.masks invalid";
            return false;
          }
          for (size_t i = 0; i < masks->items.size(); ++i) {
            int n = 0;
            if (!ToInt(masks->items[i], &n)) n = 0;
            c.cells[i] = n;
          }
        }
        s.collision.chunks.emplace(std::make_pair(c.cx, c.cy), std::move(c));
      }
    }
  }
  s.EnsureLayers();

  const JsonValue* objects = root.Find("objects");
  if (objects != nullptr) {
    if (!objects->IsArray()) {
      if (error != nullptr) *error = "scene.objects must be array";
      return false;
    }
    for (const JsonValue& ov : objects->items) {
      if (!ov.IsObject()) {
        if (error != nullptr) *error = "scene object entry must be object";
        return false;
      }
      SceneObject so;
      bool has = false;
      if (!ReadString(ov, "id", &so.id, &has, error)) return false;
      if (!ReadString(ov, "object", &so.object, &has, error) || !has ||
          so.object.empty()) {
        if (error != nullptr) *error = "scene object id missing";
        return false;
      }
      int v = 0;
      if (!ReadInt(ov, "x", &v, &has, error)) return false;
      if (has) so.x = v;
      if (!ReadInt(ov, "y", &v, &has, error)) return false;
      if (has) so.y = v;
      s.objects.push_back(std::move(so));
    }
  }
  s.EnsureSceneObjectIds();

  const JsonValue* controlled = root.Find("controlled");
  if (controlled != nullptr) {
    if (!controlled->IsArray()) {
      if (error != nullptr) *error = "scene.controlled must be array";
      return false;
    }
    for (const JsonValue& cv : controlled->items) {
      if (!cv.IsString()) continue;
      // controlled 里存的是"被引用的对象 id"（对象文档的 id），不是场景实例 id
      bool found = false;
      for (const SceneObject& so : s.objects) {
        if (so.object == cv.text || so.id == cv.text) { found = true; break; }
      }
      if (!found) continue;
      s.controlled.push_back(cv.text);
      break;  // v1 只保留第一个
    }
  }
  if (!ReadString(root, "hud_ui", &s.hud_ui, &found, error)) return false;

  const JsonValue* camera = root.Find("camera");
  if (camera != nullptr) {
    if (!camera->IsObject()) {
      if (error != nullptr) *error = "scene.camera must be object";
      return false;
    }
    if (!ReadString(*camera, "follow", &s.camera.follow, &found, error)) return false;
    if (!ReadInt(*camera, "offset_x", &s.camera.offset_x, &found, error)) return false;
    if (!ReadInt(*camera, "offset_y", &s.camera.offset_y, &found, error)) return false;
    if (!ReadInt(*camera, "deadzone_w", &s.camera.deadzone_w, &found, error)) return false;
    if (!ReadInt(*camera, "deadzone_h", &s.camera.deadzone_h, &found, error)) return false;
  }
  int gravity = 0;
  if (!ReadInt(root, "gravity", &gravity, &found, error)) return false;
  if (found) s.gravity = gravity;
  if (!ReadString(root, "bgm", &s.bgm, &found, error)) return false;
  if (!ReadString(root, "camera_mode", &s.camera_mode, &found, error)) return false;
  if (s.camera_mode.empty()) s.camera_mode = "follow";
  if (!ReadBool(root, "objects_locked", &s.objects_locked, &found, error)) return false;
  const JsonValue* triggers = root.Find("triggers");
  if (triggers != nullptr && triggers->IsArray()) {
    for (const JsonValue& tv : triggers->items) {
      if (!tv.IsObject()) continue;
      TriggerDef t;
      if (!ReadString(tv, "name", &t.name, &found, error)) return false;
      {
        // color 是 0xRRGGBBAA，可能超过 int32 → 按 double 读再转无符号
        double color = 0.0;
        if (!ReadDouble(tv, "color", &color, &found, error)) return false;
        if (found) t.color = static_cast<unsigned>(static_cast<long long>(color));
      }
      const JsonValue* itv = tv.Find("interaction");
      if (itv != nullptr && itv->IsObject()) {
        TriggerInteraction& it = t.interaction;
        it.enabled = true;
        if (!ReadString(*itv, "prompt", &it.prompt, &found, error)) return false;
        if (!ReadString(*itv, "ui_id", &it.ui_id, &found, error)) return false;
        if (!ReadString(*itv, "anchor", &it.anchor, &found, error)) return false;
        if (!ReadString(*itv, "key", &it.key, &found, error)) return false;
        if (!ReadBool(*itv, "once", &it.once, &found, error)) return false;
        if (!ReadString(*itv, "require_variable", &it.require_variable, &found, error)) return false;
        if (!ReadInt(*itv, "require_value", &it.require_value, &found, error)) return false;
        const JsonValue* acts = itv->Find("actions");
        if (acts != nullptr && acts->IsArray()) {
          for (const JsonValue& av : acts->items) {
            if (!av.IsObject()) continue;
            TriggerAction a;
            if (!ReadString(av, "type", &a.type, &found, error)) return false;
            if (!ReadString(av, "target", &a.target, &found, error)) return false;
            if (!ReadInt(av, "value", &a.value, &found, error)) return false;
            it.actions.push_back(std::move(a));
          }
        }
      }
      const JsonValue* mods = tv.Find("mods");
      if (mods != nullptr && mods->IsArray()) {
        for (const JsonValue& mv : mods->items) {
          if (!mv.IsObject()) continue;
          StatMod m;
          std::string stat, op;
          if (!ReadString(mv, "stat", &stat, &found, error)) return false;
          if (!ReadString(mv, "op", &op, &found, error)) return false;
          if (!StatIdFromName(stat, &m.id)) continue;
          m.op = (op == "add") ? StatOp::kAdd : (op == "set") ? StatOp::kSet : StatOp::kMul;
          double v = 0.0;
          if (!ReadDouble(mv, "value", &v, &found, error)) return false;
          m.value = static_cast<float>(v);
          t.mods.push_back(m);
        }
      }
      if (!ReadInt(tv, "x", &t.x, &found, error)) return false;
      if (!ReadInt(tv, "y", &t.y, &found, error)) return false;
      if (!ReadInt(tv, "w", &t.w, &found, error)) return false;
      if (!ReadInt(tv, "h", &t.h, &found, error)) return false;
      if (!ReadBool(tv, "once", &t.once, &found, error)) return false;
      if (!ReadString(tv, "target_object", &t.target_object, &found, error)) return false;
      if (!ReadString(tv, "require_variable", &t.require_variable, &found, error)) return false;
      int require_value = 0;
      if (!ReadInt(tv, "require_value", &require_value, &found, error)) return false;
      if (found) t.require_value = require_value;
      auto read_actions = [&](const JsonValue* arr, std::vector<TriggerAction>* out) {
        if (arr == nullptr || !arr->IsArray()) return true;
        for (const JsonValue& av : arr->items) {
          if (!av.IsObject()) continue;
          TriggerAction a;
          if (!ReadString(av, "type", &a.type, &found, error)) return false;
          if (!ReadString(av, "target", &a.target, &found, error)) return false;
          int value = 0;
          if (!ReadInt(av, "value", &value, &found, error)) return false;
          if (found) a.value = value;
          out->push_back(std::move(a));
        }
        return true;
      };
      int inside_n = 0;
      if (!ReadInt(tv, "inside_interval", &inside_n, &found, error)) return false;
      if (found) t.inside_interval = std::max(1, inside_n);
      const JsonValue* rules = tv.Find("rules");
      if (rules != nullptr && rules->IsArray()) {
        for (const JsonValue& rv : rules->items) {
          if (!rv.IsObject()) continue;
          TriggerRuleDef rule;
          if (!ReadBool(rv, "enabled", &rule.enabled, &found, error)) return false;
          int mode = 0;
          if (!ReadInt(rv, "mode", &mode, &found, error)) return false;
          if (found) rule.mode = mode;
          if (!ReadString(rv, "require_variable", &rule.require_variable, &found,
                          error)) {
            return false;
          }
          int rv_value = 0;
          if (!ReadInt(rv, "require_value", &rv_value, &found, error)) return false;
          if (found) rule.require_value = rv_value;
          if (!read_actions(rv.Find("actions"), &rule.actions)) return false;
          t.rules.push_back(std::move(rule));
        }
      }
      s.triggers.push_back(std::move(t));
    }
  }
  const JsonValue* vars = root.Find("variables");
  if (vars != nullptr && vars->IsObject()) {
    for (const auto& field : vars->fields) {
      if (field.second.IsNumber()) {
        s.variables[field.first] = static_cast<int>(field.second.number);
      }
    }
  }

  *out = std::move(s);
  return true;
}

void SceneSyncRaw(SceneDoc* s) {
  if (s == nullptr) return;
  s->EnsureSceneObjectIds();
  JsonValue root = s->raw.IsObject() ? s->raw : JsonValue::Object();
  root.Set("format_version", JsonValue::Number(static_cast<double>(kSceneFormatVersion)));
  s->format_version = kSceneFormatVersion;
  root.Set("id", JsonValue::String(s->id));
  root.Set("name", JsonValue::String(s->name));
  {
    JsonValue tsl = JsonValue::Array();
    for (const std::string& id : s->tilesets) tsl.Push(JsonValue::String(id));
    root.Set("tilesets", std::move(tsl));
  }
  root.Set("chunk_size", JsonValue::Number(static_cast<double>(kSceneChunkCells)));
  root.Set("map_w", JsonValue::Number(static_cast<double>(s->map_w)));
  root.Set("map_h", JsonValue::Number(static_cast<double>(s->map_h)));
  // 无边界：不写 size。
  s->EnsureLayers();
  auto write_layers = [](const std::vector<SceneLayer>& src) {
    JsonValue arr = JsonValue::Array();
    for (size_t i = 0; i < src.size(); ++i) {
      const SceneLayer& l = src[i];
      JsonValue lv = JsonValue::Object();
      lv.Set("id", JsonValue::Number(static_cast<double>(i + 1)));
      lv.Set("name", JsonValue::String(l.name));
      lv.Set("enabled", JsonValue::Bool(l.enabled));
      lv.Set("visible", JsonValue::Bool(l.visible));
      lv.Set("locked", JsonValue::Bool(l.locked));
      lv.Set("priority", JsonValue::Number(static_cast<double>(l.priority)));
      lv.Set("parallax_x", JsonValue::Number(static_cast<double>(l.parallax_x)));
      lv.Set("parallax_y", JsonValue::Number(static_cast<double>(l.parallax_y)));
      JsonValue chunks = JsonValue::Array();
      for (const auto& entry : l.chunks) {
        const SceneChunk& c = entry.second;
        JsonValue cv = JsonValue::Object();
        cv.Set("cx", JsonValue::Number(static_cast<double>(c.cx)));
        cv.Set("cy", JsonValue::Number(static_cast<double>(c.cy)));
        JsonValue cells = JsonValue::Array();
        for (int v : c.cells) cells.Push(JsonValue::Number(static_cast<double>(v)));
        cv.Set("cells", std::move(cells));
        chunks.Push(std::move(cv));
      }
      lv.Set("chunks", std::move(chunks));
      arr.Push(std::move(lv));
    }
    return arr;
  };
  root.Set("layers", write_layers(s->layers));
  root.Remove("bottom_layers");
  root.Remove("bottom_tilesets");

  JsonValue collision = JsonValue::Object();
  JsonValue cchunks = JsonValue::Array();
  for (const auto& entry : s->collision.chunks) {
    const SceneChunk& c = entry.second;
    JsonValue cv = JsonValue::Object();
    cv.Set("cx", JsonValue::Number(static_cast<double>(c.cx)));
    cv.Set("cy", JsonValue::Number(static_cast<double>(c.cy)));
    JsonValue masks = JsonValue::Array();
    for (int v : c.cells) masks.Push(JsonValue::Number(static_cast<double>(v)));
    cv.Set("masks", std::move(masks));
    cchunks.Push(std::move(cv));
  }
  collision.Set("chunks", std::move(cchunks));
  root.Set("collision", std::move(collision));

  JsonValue objects = JsonValue::Array();
  for (const SceneObject& o : s->objects) {
    JsonValue ov = JsonValue::Object();
    ov.Set("id", JsonValue::String(o.id));
    ov.Set("object", JsonValue::String(o.object));
    ov.Set("x", JsonValue::Number(static_cast<double>(o.x)));
    ov.Set("y", JsonValue::Number(static_cast<double>(o.y)));
    objects.Push(std::move(ov));
  }
  root.Set("objects", std::move(objects));

  JsonValue controlled = JsonValue::Array();
  for (const std::string& id : s->controlled) {
    controlled.Push(JsonValue::String(id));
  }
  root.Set("controlled", std::move(controlled));
  if (!s->hud_ui.empty()) {
    root.Set("hud_ui", JsonValue::String(s->hud_ui));
  } else {
    root.Remove("hud_ui");
  }

  JsonValue camera = JsonValue::Object();
  camera.Set("follow", JsonValue::String(s->camera.follow));
  camera.Set("offset_x", JsonValue::Number(static_cast<double>(s->camera.offset_x)));
  camera.Set("offset_y", JsonValue::Number(static_cast<double>(s->camera.offset_y)));
  camera.Set("deadzone_w", JsonValue::Number(static_cast<double>(s->camera.deadzone_w)));
  camera.Set("deadzone_h", JsonValue::Number(static_cast<double>(s->camera.deadzone_h)));
  root.Set("camera", std::move(camera));

  root.Set("gravity", JsonValue::Number(static_cast<double>(s->gravity)));
  root.Set("bgm", JsonValue::String(s->bgm));
  root.Set("camera_mode", JsonValue::String(
      s->camera_mode.empty() ? "follow" : s->camera_mode));
  root.Set("objects_locked", JsonValue::Bool(s->objects_locked));
  JsonValue variables = JsonValue::Object();
  for (const auto& entry : s->variables) {
    variables.Set(entry.first, JsonValue::Number(static_cast<double>(entry.second)));
  }
  root.Set("variables", std::move(variables));
  JsonValue triggers = JsonValue::Array();
  for (const TriggerDef& t : s->triggers) {
    JsonValue tv = JsonValue::Object();
    tv.Set("x", JsonValue::Number(static_cast<double>(t.x)));
    tv.Set("y", JsonValue::Number(static_cast<double>(t.y)));
    tv.Set("w", JsonValue::Number(static_cast<double>(t.w)));
    tv.Set("h", JsonValue::Number(static_cast<double>(t.h)));
    tv.Set("once", JsonValue::Bool(t.once));
    if (!t.target_object.empty()) {
      tv.Set("target_object", JsonValue::String(t.target_object));
    }
    if (!t.name.empty()) tv.Set("name", JsonValue::String(t.name));
    tv.Set("color", JsonValue::Number(static_cast<double>(t.color)));
    if (t.interaction.enabled) {
      JsonValue iv = JsonValue::Object();
      iv.Set("prompt", JsonValue::String(t.interaction.prompt));
      if (!t.interaction.ui_id.empty()) iv.Set("ui_id", JsonValue::String(t.interaction.ui_id));
      iv.Set("anchor", JsonValue::String(t.interaction.anchor));
      iv.Set("key", JsonValue::String(t.interaction.key));
      iv.Set("once", JsonValue::Bool(t.interaction.once));
      if (!t.interaction.require_variable.empty()) {
        iv.Set("require_variable", JsonValue::String(t.interaction.require_variable));
        iv.Set("require_value", JsonValue::Number(static_cast<double>(t.interaction.require_value)));
      }
      JsonValue acts = JsonValue::Array();
      for (const TriggerAction& a : t.interaction.actions) {
        JsonValue av = JsonValue::Object();
        av.Set("type", JsonValue::String(a.type));
        if (!a.target.empty()) av.Set("target", JsonValue::String(a.target));
        if (a.value != 0) av.Set("value", JsonValue::Number(static_cast<double>(a.value)));
        acts.Push(std::move(av));
      }
      iv.Set("actions", std::move(acts));
      tv.Set("interaction", std::move(iv));
    }
    {
      JsonValue mods = JsonValue::Array();
      for (const StatMod& m : t.mods) {
        JsonValue mv = JsonValue::Object();
        mv.Set("stat", JsonValue::String(StatIdName(m.id)));
        mv.Set("op", JsonValue::String(StatOpName(m.op)));
        mv.Set("value", JsonValue::Number(static_cast<double>(m.value)));
        mods.Push(std::move(mv));
      }
      tv.Set("mods", std::move(mods));
    }
    if (!t.require_variable.empty()) {
      tv.Set("require_variable", JsonValue::String(t.require_variable));
      tv.Set("require_value", JsonValue::Number(static_cast<double>(t.require_value)));
    }
    auto write_actions = [](const std::vector<TriggerAction>& list) {
      JsonValue arr = JsonValue::Array();
      for (const TriggerAction& a : list) {
        JsonValue av = JsonValue::Object();
        av.Set("type", JsonValue::String(a.type));
        if (!a.target.empty()) av.Set("target", JsonValue::String(a.target));
        av.Set("value", JsonValue::Number(static_cast<double>(a.value)));
        arr.Push(std::move(av));
      }
      return arr;
    };
    tv.Set("inside_interval",
           JsonValue::Number(static_cast<double>(t.inside_interval)));
    if (!t.rules.empty()) {
      JsonValue rules = JsonValue::Array();
      for (const TriggerRuleDef& rule : t.rules) {
        JsonValue rv = JsonValue::Object();
        rv.Set("enabled", JsonValue::Bool(rule.enabled));
        rv.Set("mode", JsonValue::Number(rule.mode));
        if (!rule.require_variable.empty()) {
          rv.Set("require_variable", JsonValue::String(rule.require_variable));
          rv.Set("require_value", JsonValue::Number(rule.require_value));
        }
        rv.Set("actions", write_actions(rule.actions));
        rules.Push(std::move(rv));
      }
      tv.Set("rules", std::move(rules));
    }
    triggers.Push(std::move(tv));
  }
  root.Set("triggers", std::move(triggers));

  s->raw = std::move(root);
}

bool SceneSave(const std::filesystem::path& path, SceneDoc* doc,
               std::string* error) {
  if (doc == nullptr || doc->id.empty()) {
    if (error != nullptr) *error = "SceneSave: empty id";
    return false;
  }
  SceneSyncRaw(doc);
  const std::string text = JsonSerialize(doc->raw, true) + "\n";
  return WriteFileText(path, text, error);
}

bool TransitionsLoad(const std::filesystem::path& path,
                     std::vector<Transition>* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) *error = "TransitionsLoad: null output";
    return false;
  }
  std::string text;
  if (!ReadFileText(path, &text, error)) return false;
  JsonValue root;
  std::string perr;
  if (!JsonParse(text, &root, &perr)) {
    if (error != nullptr) *error = perr;
    return false;
  }
  if (!root.IsObject()) {
    if (error != nullptr) *error = "transitions JSON root must be object";
    return false;
  }
  const JsonValue* fv = root.Find("format_version");
  if (fv != nullptr) {
    int v = 0;
    if (!ToInt(*fv, &v) || v < 1 || v > kSceneFormatVersion) {
      if (error != nullptr) *error = "transitions format_version unsupported";
      return false;
    }
  }
  const JsonValue* list = root.Find("transitions");
  if (list == nullptr) return true;
  if (!list->IsArray()) {
    if (error != nullptr) *error = "transitions must be array";
    return false;
  }
  for (const JsonValue& tv : list->items) {
    if (!tv.IsObject()) {
      if (error != nullptr) *error = "transition must be object";
      return false;
    }
    Transition t;
    bool found = false;
    if (!ReadString(tv, "from", &t.from, &found, error) || !found ||
        t.from.empty()) {
      if (error != nullptr) *error = "transition.from missing";
      return false;
    }
    if (!ReadString(tv, "to", &t.to, &found, error) || !found ||
        t.to.empty()) {
      if (error != nullptr) *error = "transition.to missing";
      return false;
    }
    std::string cond;
    if (!ReadString(tv, "condition", &cond, &found, error) || !found) {
      if (error != nullptr) *error = "transition.condition missing";
      return false;
    }
    if (!TransitionTypeFromName(cond, &t.type)) {
      if (error != nullptr) *error = "unsupported transition condition: " + cond;
      return false;
    }
    if (t.type == TransitionType::kSwitch) {
      if (!ReadString(tv, "value", &t.value_string, &found, error) || !found) {
        if (error != nullptr) *error = "transition switch value missing";
        return false;
      }
    }
    if (!ReadString(tv, "set_variable", &t.set_variable, &found, error)) return false;
    int set_value = 0;
    if (!ReadInt(tv, "set_value", &set_value, &found, error)) return false;
    if (found) t.set_value = set_value;
    out->push_back(std::move(t));
  }
  return true;
}

bool TransitionsSave(const std::filesystem::path& path,
                     const std::vector<Transition>& list, std::string* error) {
  JsonValue root = JsonValue::Object();
  root.Set("format_version", JsonValue::Number(static_cast<double>(kSceneFormatVersion)));
  JsonValue arr = JsonValue::Array();
  for (const Transition& t : list) {
    JsonValue tv = JsonValue::Object();
    tv.Set("from", JsonValue::String(t.from));
    tv.Set("to", JsonValue::String(t.to));
    tv.Set("condition", JsonValue::String(TransitionTypeName(t.type)));
    if (t.type == TransitionType::kSwitch) {
      tv.Set("value", JsonValue::String(t.value_string));
    }
    if (!t.set_variable.empty()) {
      tv.Set("set_variable", JsonValue::String(t.set_variable));
      tv.Set("set_value", JsonValue::Number(static_cast<double>(t.set_value)));
    }
    arr.Push(std::move(tv));
  }
  root.Set("transitions", std::move(arr));
  const std::string text = JsonSerialize(root, true) + "\n";
  return WriteFileText(path, text, error);
}

}  // namespace pgmmv