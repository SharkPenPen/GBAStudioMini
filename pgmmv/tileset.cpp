// pgmmv/tileset.cpp
#include "pgmmv/tileset.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace pgmmv {

namespace {

bool ReadFileText(const std::filesystem::path& path, std::string* out,
                  std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "cannot open tileset file: " + path.u8string();
    return false;
  }
  f.seekg(0, std::ios::end);
  const std::streamoff n = f.tellg();
  if (n < 0) {
    if (error != nullptr) *error = "cannot size tileset file";
    return false;
  }
  f.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(n));
  if (n > 0) {
    f.read(out->data(), n);
    if (!f) {
      if (error != nullptr) *error = "cannot read tileset file";
      return false;
    }
  }
  return true;
}

bool WriteFileText(const std::filesystem::path& path, const std::string& text,
                   std::string* error) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (error != nullptr) *error = "cannot write tileset file";
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
    if (error != nullptr) *error = std::string("tileset field is not int: ") + key;
    return false;
  }
  return true;
}

bool ReadString(const JsonValue& obj, const char* key, std::string* out,
                bool* found, std::string* error) {
  *found = false;
  const JsonValue* v = obj.Find(key);
  if (v == nullptr) return true;
  *found = true;
  if (!v->IsString()) {
    if (error != nullptr) *error = std::string("tileset field is not string: ") + key;
    return false;
  }
  *out = v->text;
  return true;
}

}  // namespace

std::string SanitizeDocId(const std::string& name) {
  std::string out;
  for (char c : name) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') ||
        (uc >= '0' && uc <= '9') || uc == '_' || uc == '-') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
    if (out.size() >= 48) break;
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) out = "doc";
  return out;
}

TilesetDoc TilesetCreateDefault(const std::string& id, const std::string& name,
                                const std::string& image, int tile_w, int tile_h,
                                int columns) {
  TilesetDoc d;
  d.id = id;
  d.name = name.empty() ? id : name;
  d.image = image;
  (void)tile_w;
  (void)tile_h;
  d.tile_w = 8;
  d.tile_h = 8;
  d.columns = columns > 0 ? columns : 1;
  return d;
}

bool TilesetLoad(const std::filesystem::path& path, TilesetDoc* out,
                 std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) *error = "TilesetLoad: null output";
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
    if (error != nullptr) *error = "tileset JSON root must be object";
    return false;
  }
  TilesetDoc d;
  d.raw = root;
  const JsonValue* fv = root.Find("format_version");
  if (fv != nullptr) {
    int v = 0;
    if (!ToInt(*fv, &v) || v < 1 || v > kTilesetFormatVersion) {
      if (error != nullptr) *error = "tileset format_version unsupported";
      return false;
    }
    d.format_version = v;
  }
  bool found = false;
  if (!ReadString(root, "id", &d.id, &found, error)) return false;
  if (d.id.empty()) d.id = path.stem().u8string();
  if (!ReadString(root, "name", &d.name, &found, error)) return false;
  if (d.name.empty()) d.name = d.id;
  if (!ReadString(root, "image", &d.image, &found, error)) return false;
  int v = 0;
  if (!ReadInt(root, "tile_w", &v, &found, error)) return false;
  if (found) d.tile_w = v;
  if (!ReadInt(root, "tile_h", &v, &found, error)) return false;
  if (found) d.tile_h = v;
  if (!ReadInt(root, "columns", &v, &found, error)) return false;
  if (found) d.columns = v;
  if (!ReadInt(root, "offset_x", &v, &found, error)) return false;
  if (found) d.offset_x = std::max(0, v);
  if (!ReadInt(root, "offset_y", &v, &found, error)) return false;
  if (found) d.offset_y = std::max(0, v);
  if (d.tile_w < 8 || d.tile_h < 8 || (d.tile_w % 8) != 0 ||
      (d.tile_h % 8) != 0 || d.columns <= 0) {
    if (error != nullptr) {
      *error = "tileset tile size must be >=8 and a multiple of 8";
    }
    return false;
  }

  // 老文件里的 "tiles"（wall/gimmick/kind/...）直接忽略；保存时会被丢掉。

  *out = std::move(d);
  return true;
}

void TilesetSyncRaw(TilesetDoc* d) {
  if (d == nullptr) return;
  JsonValue root = d->raw.IsObject() ? d->raw : JsonValue::Object();
  d->format_version = kTilesetFormatVersion;
  root.Set("format_version", JsonValue::Number(static_cast<double>(d->format_version)));
  root.Set("id", JsonValue::String(d->id));
  root.Set("name", JsonValue::String(d->name));
  root.Set("image", JsonValue::String(d->image));
  root.Set("tile_w", JsonValue::Number(static_cast<double>(d->tile_w)));
  root.Set("tile_h", JsonValue::Number(static_cast<double>(d->tile_h)));
  root.Set("columns", JsonValue::Number(static_cast<double>(d->columns)));
  root.Set("offset_x", JsonValue::Number(static_cast<double>(d->offset_x)));
  root.Set("offset_y", JsonValue::Number(static_cast<double>(d->offset_y)));
  // v2：tiles[] 不再存在。老文件里残留的那个键在保存时清掉。
  root.Remove("tiles");
  d->raw = std::move(root);
}

bool TilesetSave(const std::filesystem::path& path, TilesetDoc* doc,
                 std::string* error) {
  if (doc == nullptr || doc->id.empty()) {
    if (error != nullptr) *error = "TilesetSave: empty id";
    return false;
  }
  TilesetSyncRaw(doc);
  const std::string text = JsonSerialize(doc->raw, true) + "\n";
  return WriteFileText(path, text, error);
}

}  // namespace pgmmv
