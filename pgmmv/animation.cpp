#include "pgmmv/animation.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace pgmmv {

AnimationDoc AnimationCreateForObject(const std::string& id, const std::string& name,
                                      int frame_w, int frame_h) {
  AnimationDoc ad = AnimationCreateDefault(id, name);
  ad.frame_w = frame_w;
  ad.frame_h = frame_h;
  auto add = [&ad, frame_w, frame_h](const char* n, std::vector<int> images,
                                     int fps, bool loop) {
    ActionDef a;
    a.name = n;
    for (int img : images) {
      ActionFrame f;
      f.image = img;
      a.frames.push_back(f);
    }
    a.origin_x = frame_w / 2;
    a.origin_y = frame_h;  // 脚下中心
    a.fps = fps;
    a.loop = loop;
    ad.actions.push_back(std::move(a));
  };
  add("idle", {0}, 4, true);
  add("run", {1, 2}, 8, true);
  add("jump", {3}, 1, false);
  add("fall", {3}, 8, true);
  return ad;
}

bool AnimationLoad(const std::filesystem::path& path, AnimationDoc* out,
                   std::string* error) {
  if (out == nullptr) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "cannot open " + path.u8string();
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  JsonValue root;
  if (!JsonParse(ss.str(), &root, error)) return false;
  if (!root.IsObject()) {
    if (error != nullptr) *error = "animation root must be object";
    return false;
  }
  AnimationDoc doc;
  doc.raw = root;
  if (const JsonValue* v = root.Find("id"); v != nullptr && v->IsString()) {
    doc.id = v->text;
  }
  if (const JsonValue* v = root.Find("name"); v != nullptr && v->IsString()) {
    doc.name = v->text;
  }
  if (const JsonValue* v = root.Find("image"); v != nullptr && v->IsString()) {
    doc.image = v->text;
  }
  if (const JsonValue* v = root.Find("frame_w"); v != nullptr && v->IsNumber()) {
    doc.frame_w = static_cast<int>(v->number);
  }
  if (const JsonValue* v = root.Find("frame_h"); v != nullptr && v->IsNumber()) {
    doc.frame_h = static_cast<int>(v->number);
  }
  if (const JsonValue* arr = root.Find("actions");
      arr != nullptr && arr->IsArray()) {
    for (const JsonValue& item : arr->items) {
      ActionDef act;
      if (!ReadDirectionForResource(item, &act, error)) return false;
      if (const JsonValue* nv = item.Find("name");
          nv != nullptr && nv->IsString()) {
        act.name = nv->text;
      }
      if (const JsonValue* bv = item.Find("base_variant");
          bv != nullptr && bv->IsString() && !bv->text.empty()) {
        act.base_variant = bv->text;
      }
      if (const JsonValue* dirs = item.Find("directions");
          dirs != nullptr && dirs->IsArray()) {
        for (const JsonValue& dv : dirs->items) {
          DirectionDef d;
          if (!ReadDirectionForResource(dv, &d, error)) return false;
          act.directions.push_back(std::move(d));
        }
      }
      doc.actions.push_back(std::move(act));
    }
  }
  *out = std::move(doc);
  return true;
}

bool AnimationSave(const std::filesystem::path& path, AnimationDoc* doc,
                   std::string* error) {
  if (doc == nullptr) return false;
  JsonValue root = doc->raw.IsObject() ? doc->raw : JsonValue::Object();
  root.Set("format_version", JsonValue::Number(kAnimationFormatVersion));
  root.Set("id", JsonValue::String(doc->id));
  root.Set("name", JsonValue::String(doc->name));
  root.Set("image", JsonValue::String(doc->image));
  root.Set("frame_w", JsonValue::Number(static_cast<double>(doc->frame_w)));
  root.Set("frame_h", JsonValue::Number(static_cast<double>(doc->frame_h)));
  JsonValue arr = JsonValue::Array();
  for (const ActionDef& action : doc->actions) {
    JsonValue v = WriteDirectionForResource(action);
    v.Set("name", JsonValue::String(action.name));
    v.Set("base_variant", JsonValue::String(action.base_variant));
    JsonValue dirs = JsonValue::Array();
    for (const DirectionDef& d : action.directions) {
      dirs.Push(WriteDirectionForResource(d));
    }
    v.Set("directions", std::move(dirs));
    arr.Push(std::move(v));
  }
  root.Set("actions", std::move(arr));
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "cannot write " + path.u8string();
    return false;
  }
  f << JsonSerialize(root, true) << "\n";
  doc->raw = std::move(root);
  return true;
}

}  // namespace pgmmv
