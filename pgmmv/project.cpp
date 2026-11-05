// pgmmv/project.cpp
#include "pgmmv/project.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <system_error>

#include "render/png.h"

namespace pgmmv {

namespace {

constexpr int kMaxImageSize = 2048;

std::string PathText(const std::filesystem::path& p) { return p.u8string(); }

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>* out,
                   std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) {
      *error = "cannot open file: " + PathText(path);
    }
    return false;
  }
  f.seekg(0, std::ios::end);
  const std::streamoff n = f.tellg();
  if (n < 0) {
    if (error != nullptr) {
      *error = "cannot get file size: " + PathText(path);
    }
    return false;
  }
  f.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(n));
  if (n > 0) {
    f.read(reinterpret_cast<char*>(out->data()), n);
    if (!f) {
      if (error != nullptr) {
        *error = "cannot read file: " + PathText(path);
      }
      return false;
    }
  }
  return true;
}

bool WriteFileBytes(const std::filesystem::path& path, const std::string& data,
                    std::string* error) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (error != nullptr) {
      *error = "cannot write file: " + PathText(path);
    }
    return false;
  }
  if (!data.empty()) {
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  if (!f) {
    if (error != nullptr) {
      *error = "write failed: " + PathText(path);
    }
    return false;
  }
  return true;
}

bool NumberToInt(const JsonValue& v, int* out) {
  if (!v.IsNumber()) {
    return false;
  }
  const double d = v.number;
  if (!std::isfinite(d) || d < -2147483648.0 || d > 2147483647.0) {
    return false;
  }
  if (d != std::floor(d)) {
    return false;
  }
  *out = static_cast<int>(d);
  return true;
}

bool ReadOptionalInt(const JsonValue& obj, const char* key, int* out, bool* found,
                     std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) {
    return true;
  }
  *found = true;
  if (!NumberToInt(*v, out)) {
    if (error != nullptr) {
      *error = std::string("field is not an integer: ") + key;
    }
    return false;
  }
  return true;
}

bool ReadOptionalString(const JsonValue& obj, const char* key, std::string* out,
                        bool* found, std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) {
    return true;
  }
  *found = true;
  if (!v->IsString()) {
    if (error != nullptr) {
      *error = std::string("field is not a string: ") + key;
    }
    return false;
  }
  *out = v->text;
  return true;
}

std::uint32_t Be32(const unsigned char* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

bool ReadPngSize(const std::vector<unsigned char>& bytes, int* width, int* height,
                 std::string* error) {
  static const unsigned char kMagic[8] = {0x89, 0x50, 0x4E, 0x47,
                                          0x0D, 0x0A, 0x1A, 0x0A};
  if (bytes.size() < 33 || std::memcmp(bytes.data(), kMagic, 8) != 0) {
    if (error != nullptr) {
      *error = "not a PNG file";
    }
    return false;
  }
  const std::uint32_t len = Be32(bytes.data() + 8);
  if (len != 13 || std::memcmp(bytes.data() + 12, "IHDR", 4) != 0) {
    if (error != nullptr) {
      *error = "PNG has no valid IHDR";
    }
    return false;
  }
  const std::uint32_t w = Be32(bytes.data() + 16);
  const std::uint32_t h = Be32(bytes.data() + 20);
  if (w == 0 || h == 0) {
    if (error != nullptr) {
      *error = "PNG has zero size";
    }
    return false;
  }
  if (w > static_cast<std::uint32_t>(kMaxImageSize) ||
      h > static_cast<std::uint32_t>(kMaxImageSize)) {
    if (error != nullptr) {
      *error = "PNG is too large for R1 (max 2048x2048)";
    }
    return false;
  }
  *width = static_cast<int>(w);
  *height = static_cast<int>(h);
  return true;
}

std::string SanitizeId(const std::string& stem) {
  std::string out;
  for (char c : stem) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') ||
        (uc >= '0' && uc <= '9') || uc == '_' || uc == '-') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
    if (out.size() >= 64) {
      break;
    }
  }
  while (!out.empty() && out.back() == '_') {
    out.pop_back();
  }
  if (out.empty()) {
    out = "image";
  }
  return out;
}

std::string MakeUniqueAssetId(const Project& p, const std::string& base) {
  if (p.FindAsset(base) == nullptr) {
    return base;
  }
  for (int i = 2; i < 10000; ++i) {
    const std::string candidate = base + "_" + std::to_string(i);
    if (p.FindAsset(candidate) == nullptr) {
      return candidate;
    }
  }
  return base + "_x";
}

}  // namespace

const char* AssetKindName(AssetKind kind) {
  switch (kind) {
    case AssetKind::kImage: return "image";
    case AssetKind::kText: return "text";
    case AssetKind::kSwitch: return "switch";
    case AssetKind::kVariable: return "variable";
    case AssetKind::kAudio: return "audio";
    case AssetKind::kMesh: return "mesh";
    case AssetKind::kMaterial: return "material";
    case AssetKind::kUnknown: return "unknown";
  }
  return "unknown";
}

AssetKind AssetKindFromName(const std::string& name) {
  if (name == "image") return AssetKind::kImage;
  if (name == "text") return AssetKind::kText;
  if (name == "switch") return AssetKind::kSwitch;
  if (name == "variable") return AssetKind::kVariable;
  if (name == "audio") return AssetKind::kAudio;
  if (name == "mesh") return AssetKind::kMesh;
  if (name == "material") return AssetKind::kMaterial;
  return AssetKind::kUnknown;
}

const Asset* Project::FindAsset(const std::string& id) const {
  for (const Asset& a : assets) {
    if (a.id == id) {
      return &a;
    }
  }
  return nullptr;
}

Asset* Project::FindAsset(const std::string& id) {
  for (Asset& a : assets) {
    if (a.id == id) {
      return &a;
    }
  }
  return nullptr;
}

const Asset* Project::FindAssetByPath(const std::string& path) const {
  for (const Asset& a : assets) {
    if (a.path == path) {
      return &a;
    }
  }
  return nullptr;
}

const ObjectDoc* Project::FindObject(const std::string& id) const {
  for (const ObjectDoc& o : objects) {
    if (o.id == id) {
      return &o;
    }
  }
  return nullptr;
}

ObjectDoc* Project::FindObject(const std::string& id) {
  for (ObjectDoc& o : objects) {
    if (o.id == id) {
      return &o;
    }
  }
  return nullptr;
}

const TilesetDoc* Project::FindTileset(const std::string& id) const {
  for (const TilesetDoc& t : tilesets) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

TilesetDoc* Project::FindTileset(const std::string& id) {
  for (TilesetDoc& t : tilesets) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

const SceneDoc* Project::FindScene(const std::string& id) const {
  for (const SceneDoc& s : scenes) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

SceneDoc* Project::FindScene(const std::string& id) {
  for (SceneDoc& s : scenes) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

std::filesystem::path ProjectFilePath(const Project& p) {
  return p.root_dir / "project.json";
}

std::filesystem::path ProjectAssetPath(const Project& p, const Asset& a) {
  const std::filesystem::path rel = std::filesystem::u8path(a.path);
  if (rel.is_absolute()) {
    return rel;
  }
  return p.root_dir / rel;
}

std::filesystem::path ProjectObjectPath(const Project& p, const ObjectDoc& o) {
  return p.root_dir / "objects" / (o.id + ".json");
}

std::filesystem::path ProjectTilesetPath(const Project& p, const TilesetDoc& t) {
  return p.root_dir / "tiles" / (t.id + ".json");
}

std::filesystem::path ProjectScenePath(const Project& p, const SceneDoc& s) {
  return p.root_dir / "scenes" / (s.id + ".json");
}

std::filesystem::path ProjectTransitionsPath(const Project& p) {
  return p.root_dir / "transitions.json";
}

void ProjectSyncRaw(Project* p) {
  if (p == nullptr) {
    return;
  }
  JsonValue root = p->raw.IsObject() ? p->raw : JsonValue::Object();
  {
    JsonValue vars = JsonValue::Object();
    for (const auto& kv : p->variables) {
      vars.Set(kv.first, JsonValue::Number(static_cast<double>(kv.second)));
    }
    root.Set("variables", std::move(vars));
  }

  root.Set("format_version", JsonValue::Number(static_cast<double>(p->format_version)));
  root.Set("name", JsonValue::String(p->name));

  JsonValue settings =
      (root.Find("settings") != nullptr && root.Find("settings")->IsObject())
          ? *root.Find("settings")
          : JsonValue::Object();
  settings.Set("initial_scene", JsonValue::String(p->settings.initial_scene));
  root.Set("settings", std::move(settings));
  root.Set("persistent_ui", JsonValue::String(p->persistent_ui));
  root.Set("title_ui", JsonValue::String(p->title_ui));

  JsonValue assets = JsonValue::Array();
  for (const Asset& a : p->assets) {
    JsonValue obj = a.raw.IsObject() ? a.raw : JsonValue::Object();
    obj.Set("id", JsonValue::String(a.id));
    obj.Set("name", JsonValue::String(a.name));
    obj.Set("type", JsonValue::String(a.type.empty() ? AssetKindName(a.kind) : a.type));
    obj.Set("path", JsonValue::String(a.path));
    if (!a.source.empty()) obj.Set("source", JsonValue::String(a.source));
    obj.Set("width", JsonValue::Number(static_cast<double>(a.width)));
    obj.Set("height", JsonValue::Number(static_cast<double>(a.height)));
    obj.Set("split_x", JsonValue::Number(static_cast<double>(a.split_x)));
    obj.Set("split_y", JsonValue::Number(static_cast<double>(a.split_y)));
    assets.Push(std::move(obj));
  }
  root.Set("assets", std::move(assets));

  p->raw = std::move(root);
}

bool ProjectCreate(const std::filesystem::path& dir, const std::string& name,
                   Project* out, std::string* error) {
  if (out == nullptr || dir.empty()) {
    if (error != nullptr) {
      *error = "ProjectCreate: empty directory";
    }
    return false;
  }

  std::error_code ec;
  const std::filesystem::path abs = std::filesystem::absolute(dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "ProjectCreate: " + ec.message();
    }
    return false;
  }
  const std::filesystem::path project_file = abs / "project.json";
  if (std::filesystem::exists(project_file, ec)) {
    if (error != nullptr) {
      *error = "project already exists: " + PathText(project_file);
    }
    return false;
  }

  std::filesystem::create_directories(abs / "assets", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create project directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(abs / "objects", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create objects directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(abs / "tiles", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create tiles directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(abs / "scenes", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create scenes directory: " + ec.message();
    }
    return false;
  }

  Project p;
  p.root_dir = abs;
  p.raw = JsonValue::Object();
  p.format_version = kProjectFormatVersion;
  p.name = name.empty() ? "Untitled" : name;
  ProjectSyncRaw(&p);
  if (!ProjectSave(&p, error)) {
    return false;
  }
  *out = std::move(p);
  return true;
}

bool ProjectLoad(const std::filesystem::path& project_json_path, Project* out,
                 std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "ProjectLoad: output is null";
    }
    return false;
  }

  std::vector<unsigned char> bytes;
  if (!ReadFileBytes(project_json_path, &bytes, error)) {
    return false;
  }
  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  JsonValue root;
  std::string parse_error;
  if (!JsonParse(text, &root, &parse_error)) {
    if (error != nullptr) {
      *error = parse_error;
    }
    return false;
  }
  if (!root.IsObject()) {
    if (error != nullptr) {
      *error = "project.json root must be an object";
    }
    return false;
  }

  Project p;
  std::error_code ec;
  const std::filesystem::path abs =
      std::filesystem::absolute(project_json_path, ec);
  p.root_dir = ec ? project_json_path.parent_path() : abs.parent_path();
  p.raw = root;

  {
    const JsonValue* pv = root.Find("variables");
    if (pv != nullptr && pv->IsObject()) {
      for (const auto& kv : pv->fields) {
        int val = 0;
        if (NumberToInt(kv.second, &val)) p.variables[kv.first] = val;
      }
    }
  }
  const JsonValue* fv = root.Find("format_version");
  if (fv != nullptr) {
    int v = 0;
    if (!NumberToInt(*fv, &v) || v < 1 || v > kProjectFormatVersion) {
      if (error != nullptr) {
        *error = "unsupported format_version";
      }
      return false;
    }
    p.format_version = v;
  }

  bool found = false;
  if (!ReadOptionalString(root, "name", &p.name, &found, error)) {
    return false;
  }

  const JsonValue* settings = root.Find("settings");
  if (settings != nullptr) {
    if (!settings->IsObject()) {
      if (error != nullptr) {
        *error = "settings must be an object";
      }
      return false;
    }
    if (!ReadOptionalString(*settings, "initial_scene", &p.settings.initial_scene, &found, error)) {
      return false;
    }
  }
  if (!ReadOptionalString(root, "persistent_ui", &p.persistent_ui, &found, error)) {
    return false;
  }
  if (!ReadOptionalString(root, "title_ui", &p.title_ui, &found, error)) {
    return false;
  }

  const JsonValue* assets = root.Find("assets");
  if (assets != nullptr) {
    if (!assets->IsArray()) {
      if (error != nullptr) {
        *error = "assets must be an array";
      }
      return false;
    }
    for (const JsonValue& item : assets->items) {
      if (!item.IsObject()) {
        if (error != nullptr) {
          *error = "asset must be an object";
        }
        return false;
      }
      Asset a;
      bool has = false;
      if (!ReadOptionalString(item, "id", &a.id, &has, error) || !has ||
          a.id.empty()) {
        if (error != nullptr) *error = "asset.id is required";
        return false;
      }
      if (!ReadOptionalString(item, "source", &a.source, &has, error)) return false;
      if (!ReadOptionalString(item, "path", &a.path, &has, error) || !has ||
          a.path.empty()) {
        if (error != nullptr) *error = "asset.path is required";
        return false;
      }
      if (!ReadOptionalString(item, "name", &a.name, &has, error)) return false;
      if (!ReadOptionalString(item, "type", &a.type, &has, error)) return false;
      a.kind = AssetKindFromName(a.type);
      if (!ReadOptionalInt(item, "width", &a.width, &has, error)) return false;
      if (!ReadOptionalInt(item, "height", &a.height, &has, error)) return false;
      if (!ReadOptionalInt(item, "split_x", &a.split_x, &has, error)) return false;
      if (!ReadOptionalInt(item, "split_y", &a.split_y, &has, error)) return false;
      if (a.split_x < 1) a.split_x = 1;
      if (a.split_y < 1) a.split_y = 1;
      a.raw = item;
      if (p.FindAsset(a.id) != nullptr) {
        if (error != nullptr) {
          *error = "duplicate asset id: " + a.id;
        }
        return false;
      }
      p.assets.push_back(std::move(a));
    }
  }

  // ---- objects/*.json ----
  const std::filesystem::path obj_dir = p.root_dir / "objects";
  std::error_code oe;
  if (std::filesystem::exists(obj_dir, oe)) {
    for (const auto& entry : std::filesystem::directory_iterator(obj_dir, oe)) {
      if (oe) break;
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".json") continue;
      ObjectDoc obj;
      std::string oerr;
      if (!ObjectLoad(entry.path(), &obj, &oerr)) {
        if (error != nullptr) *error = "object load failed: " + oerr;
        return false;
      }
      if (p.FindObject(obj.id) != nullptr) {
        if (error != nullptr) *error = "duplicate object id: " + obj.id;
        return false;
      }
      p.objects.push_back(std::move(obj));
    }
    if (oe) {
      if (error != nullptr) *error = "cannot scan objects directory: " + oe.message();
      return false;
    }
  }

  // ---- tiles/*.json ----
  const std::filesystem::path tile_dir = p.root_dir / "tiles";
  std::error_code te;
  if (std::filesystem::exists(tile_dir, te)) {
    for (const auto& entry : std::filesystem::directory_iterator(tile_dir, te)) {
      if (te) break;
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".json") continue;
      TilesetDoc ts;
      std::string terr;
      if (!TilesetLoad(entry.path(), &ts, &terr)) {
        if (error != nullptr) *error = "tileset load failed: " + terr;
        return false;
      }
      if (p.FindTileset(ts.id) != nullptr) {
        if (error != nullptr) *error = "duplicate tileset id: " + ts.id;
        return false;
      }
      p.tilesets.push_back(std::move(ts));
    }
    if (te) {
      if (error != nullptr) *error = "cannot scan tiles directory: " + te.message();
      return false;
    }
  }

  // ---- scenes/*.json ----
  const std::filesystem::path scene_dir = p.root_dir / "scenes";
  std::error_code se;
  if (std::filesystem::exists(scene_dir, se)) {
    for (const auto& entry : std::filesystem::directory_iterator(scene_dir, se)) {
      if (se) break;
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".json") continue;
      SceneDoc sc;
      std::string serr;
      if (!SceneLoad(entry.path(), &sc, &serr)) {
        if (error != nullptr) *error = "scene load failed: " + serr;
        return false;
      }
      if (p.FindScene(sc.id) != nullptr) {
        if (error != nullptr) *error = "duplicate scene id: " + sc.id;
        return false;
      }
      p.scenes.push_back(std::move(sc));
    }
    if (se) {
      if (error != nullptr) *error = "cannot scan scenes directory: " + se.message();
      return false;
    }
  }

  // ---- ui/*.json ----
  const std::filesystem::path ui_dir = p.root_dir / "ui";
  std::error_code ue;
  if (std::filesystem::exists(ui_dir, ue)) {
    for (const auto& entry : std::filesystem::directory_iterator(ui_dir, ue)) {
      if (ue) break;
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".json") continue;
      UiScreenDoc screen;
      std::string uerr;
      if (!UiScreenLoad(entry.path(), &screen, &uerr)) {
        if (error != nullptr) *error = "ui load failed: " + uerr;
        return false;
      }
      p.ui_screens.push_back(std::move(screen));
    }
    if (ue) {
      if (error != nullptr) *error = "cannot scan ui directory: " + ue.message();
      return false;
    }
  }

  // ---- animations/*.json ----
  const std::filesystem::path anim_dir = p.root_dir / "animations";
  std::error_code ae;
  if (std::filesystem::exists(anim_dir, ae)) {
    for (const auto& entry : std::filesystem::directory_iterator(anim_dir, ae)) {
      if (ae) break;
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".json") continue;
      AnimationDoc ad;
      std::string aerr;
      if (!AnimationLoad(entry.path(), &ad, &aerr)) {
        if (error != nullptr) *error = "animation load failed: " + aerr;
        return false;
      }
      p.animations.push_back(std::move(ad));
    }
    if (ae) {
      if (error != nullptr) *error = "cannot scan animations directory: " + ae.message();
      return false;
    }
  }
  // 动画由用户在对象页显式选择；这里不再偷偷补 <对象id>_anim。
  // 引用已经不存在的动画时清空，避免对象页下拉看起来“有引用但读不到”。
  for (ObjectDoc& o : p.objects) {
    if (!o.animation.empty() && FindAnimation(p, o.animation) == nullptr) {
      o.animation.clear();
    }
  }

  // ---- transitions.json（可选）----
  const std::filesystem::path trans_path = ProjectTransitionsPath(p);
  std::error_code tre;
  if (std::filesystem::exists(trans_path, tre)) {
    std::string terr;
    if (!TransitionsLoad(trans_path, &p.transitions, &terr)) {
      if (error != nullptr) *error = "transitions load failed: " + terr;
      return false;
    }
  }

  *out = std::move(p);
  return true;
}

bool ProjectSave(Project* p, std::string* error) {
  if (p == nullptr || p->root_dir.empty()) {
    if (error != nullptr) {
      *error = "ProjectSave: empty project root";
    }
    return false;
  }
  auto check_unique = [&](const std::vector<std::string>& ids,
                          const char* what) -> bool {
    std::set<std::string> seen;
    for (const std::string& id : ids) {
      if (id.empty()) {
        if (error != nullptr) *error = std::string(what) + " has empty id";
        return false;
      }
      if (!seen.insert(id).second) {
        if (error != nullptr) {
          *error = std::string(what) + " duplicate id: " + id;
        }
        return false;
      }
    }
    return true;
  };
  auto ids_of = [](auto&& list) {
    std::vector<std::string> out;
    for (const auto& item : list) out.push_back(item.id);
    return out;
  };
  if (!check_unique(ids_of(p->assets), "asset") ||
      !check_unique(ids_of(p->objects), "object") ||
      !check_unique(ids_of(p->animations), "animation") ||
      !check_unique(ids_of(p->tilesets), "tileset") ||
      !check_unique(ids_of(p->scenes), "scene") ||
      !check_unique(ids_of(p->ui_screens), "ui screen")) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(p->root_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create project directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(p->root_dir / "assets", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create assets directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(p->root_dir / "objects", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create objects directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(p->root_dir / "tiles", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create tiles directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(p->root_dir / "animations", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create animations directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(p->root_dir / "scenes", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create scenes directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(p->root_dir / "ui", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create ui directory: " + ec.message();
    }
    return false;
  }

  // 对象/图块/场景先落盘；project.json 里不重复存它们。
  for (ObjectDoc& o : p->objects) {
    if (o.id.empty()) {
      if (error != nullptr) *error = "object has empty id";
      return false;
    }
    if (!ObjectSave(ProjectObjectPath(*p, o), &o, error)) {
      return false;
    }
  }
  for (TilesetDoc& t : p->tilesets) {
    if (t.id.empty()) {
      if (error != nullptr) *error = "tileset has empty id";
      return false;
    }
    if (!TilesetSave(ProjectTilesetPath(*p, t), &t, error)) {
      return false;
    }
  }
  for (SceneDoc& s : p->scenes) {
    if (s.id.empty()) {
      if (error != nullptr) *error = "scene has empty id";
      return false;
    }
    if (!SceneSave(ProjectScenePath(*p, s), &s, error)) {
      return false;
    }
  }
  for (UiScreenDoc& s : p->ui_screens) {
    if (s.id.empty()) {
      if (error != nullptr) *error = "ui screen has empty id";
      return false;
    }
    if (!UiScreenSave(p->root_dir / "ui" / (s.id + ".json"), &s, error)) {
      return false;
    }
  }
  {
    // 删掉已经不存在的动画文件，否则下次加载又会把它们捡回来
    const std::filesystem::path anim_dir = p->root_dir / "animations";
    std::error_code de;
    if (std::filesystem::exists(anim_dir, de)) {
      for (const auto& entry : std::filesystem::directory_iterator(anim_dir, de)) {
        if (de) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        const std::string stem = entry.path().stem().u8string();
        bool keep = false;
        for (const AnimationDoc& a : p->animations) {
          if (a.id == stem) keep = true;
        }
        if (!keep) {
          std::error_code re;
          std::filesystem::remove(entry.path(), re);
        }
      }
    }
  }
  for (AnimationDoc& a : p->animations) {
    if (a.id.empty()) {
      if (error != nullptr) *error = "animation has empty id";
      return false;
    }
    const std::filesystem::path ap = p->root_dir / "animations" / (a.id + ".json");
    if (!AnimationSave(ap, &a, error)) return false;
  }
  if (!TransitionsSave(ProjectTransitionsPath(*p), p->transitions, error)) {
    return false;
  }

  ProjectSyncRaw(p);
  const std::string text = JsonSerialize(p->raw, true) + "\n";
  return WriteFileBytes(ProjectFilePath(*p), text, error);
}

bool LoadPngRgba(const std::filesystem::path& path,
                 std::vector<unsigned char>* rgba, int* width, int* height,
                 std::string* error) {
  if (rgba == nullptr || width == nullptr || height == nullptr) {
    if (error != nullptr) {
      *error = "LoadPngRgba: null output";
    }
    return false;
  }
  std::vector<unsigned char> bytes;
  if (!ReadFileBytes(path, &bytes, error)) {
    return false;
  }
  int w = 0;
  int h = 0;
  if (!ReadPngSize(bytes, &w, &h, error)) {
    return false;
  }
  std::vector<unsigned char> out(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
  std::vector<unsigned char> scratch(
      static_cast<size_t>(eng::PngScratchBytes(w, h, 4)));
  eng::PngImage img;
  char why[256] = {};
  if (!eng::PngDecode(bytes.data(), static_cast<int>(bytes.size()), out.data(),
                      static_cast<int>(out.size()), scratch.data(),
                      static_cast<int>(scratch.size()), &img, why, sizeof(why))) {
    if (error != nullptr) {
      *error = std::string("PNG decode failed: ") + why;
    }
    return false;
  }
  if (img.width != w || img.height != h) {
    if (error != nullptr) {
      *error = "PNG size mismatch";
    }
    return false;
  }
  *rgba = std::move(out);
  *width = w;
  *height = h;
  return true;
}

bool ProjectImportPng(Project* p, const std::filesystem::path& source_path,
                      std::string* new_asset_id, std::string* error) {
  if (p == nullptr || p->root_dir.empty()) {
    if (error != nullptr) {
      *error = "ProjectImportPng: project has no root";
    }
    return false;
  }

  std::vector<unsigned char> pixels;
  int w = 0;
  int h = 0;
  if (!LoadPngRgba(source_path, &pixels, &w, &h, error)) {
    return false;
  }

  const std::string stem = source_path.stem().u8string();
  const std::string base = SanitizeId(stem);
  const std::string id = MakeUniqueAssetId(*p, base);
  const std::string rel = "assets/" + id + ".png";
  const std::filesystem::path target = p->root_dir / std::filesystem::u8path(rel);

  std::error_code ec;
  std::filesystem::create_directories(p->root_dir / "assets", ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create assets directory: " + ec.message();
    }
    return false;
  }
  std::error_code same_ec;
  const bool same_file = std::filesystem::equivalent(source_path, target, same_ec);
  if (!same_file) {
    std::filesystem::copy_file(source_path, target,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      if (error != nullptr) {
        *error = "cannot copy PNG: " + ec.message();
      }
      return false;
    }
  }

  Asset a;
  a.id = id;
  a.name = stem.empty() ? id : stem;
  a.type = "image";
  a.kind = AssetKind::kImage;
  a.path = rel;
  a.width = w;
  a.height = h;
  a.raw = JsonValue::Object();
  p->assets.push_back(std::move(a));

  if (new_asset_id != nullptr) {
    *new_asset_id = id;
  }
  return true;
}

}  // namespace pgmmv
