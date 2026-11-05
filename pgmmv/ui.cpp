#include "pgmmv/ui.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace pgmmv {
namespace {

void ReadStr(const JsonValue& o, const char* key, std::string* v) {
  const JsonValue* p = o.Find(key);
  if (p != nullptr && p->IsString()) *v = p->text;
}

void ReadInt(const JsonValue& o, const char* key, int* v) {
  const JsonValue* p = o.Find(key);
  if (p != nullptr && p->IsNumber()) *v = static_cast<int>(p->number);
}

void ReadUInt(const JsonValue& o, const char* key, unsigned* v) {
  const JsonValue* p = o.Find(key);
  if (p != nullptr && p->IsNumber()) *v = static_cast<unsigned>(p->number);
}

void ReadBool(const JsonValue& o, const char* key, bool* v) {
  const JsonValue* p = o.Find(key);
  if (p != nullptr && p->IsBool()) *v = p->boolean;
}

void ReadElement(const JsonValue& v, UiElementDoc* e) {
  if (!v.IsObject()) return;
  e->raw = v;
  ReadStr(v, "id", &e->id);
  ReadStr(v, "kind", &e->kind);
  ReadStr(v, "anchor", &e->anchor);
  ReadInt(v, "x", &e->x);
  ReadInt(v, "y", &e->y);
  ReadInt(v, "w", &e->w);
  ReadInt(v, "h", &e->h);
  ReadInt(v, "slice_l", &e->slice_l);
  ReadInt(v, "slice_r", &e->slice_r);
  ReadInt(v, "slice_t", &e->slice_t);
  ReadInt(v, "slice_b", &e->slice_b);
  ReadBool(v, "slice_tile", &e->slice_tile);
  ReadStr(v, "image", &e->image);
  ReadInt(v, "src_x", &e->src_x);
  ReadInt(v, "src_y", &e->src_y);
  ReadInt(v, "src_w", &e->src_w);
  ReadInt(v, "src_h", &e->src_h);
  ReadInt(v, "frames", &e->frames);
  ReadInt(v, "frame", &e->frame);
  ReadStr(v, "text", &e->text);
  ReadInt(v, "font_size", &e->font_size);
  ReadUInt(v, "color", &e->color);
  ReadInt(v, "align", &e->align);
  ReadStr(v, "value_var", &e->value_var);
  ReadInt(v, "map", &e->map);
  ReadInt(v, "in_min", &e->in_min);
  ReadInt(v, "in_max", &e->in_max);
  ReadInt(v, "out_min", &e->out_min);
  ReadInt(v, "out_max", &e->out_max);
  ReadStr(v, "format", &e->format);
  ReadStr(v, "visible_var", &e->visible_var);
  ReadInt(v, "visible_value", &e->visible_value);
  ReadInt(v, "padding", &e->padding);
  const JsonValue* children = v.Find("children");
  if (children != nullptr && children->IsArray()) {
    for (const JsonValue& cv : children->items) {
      UiElementDoc c;
      ReadElement(cv, &c);
      e->children.push_back(std::move(c));
    }
  }
}

JsonValue WriteElement(const UiElementDoc& e) {
  JsonValue o = e.raw.IsObject() ? e.raw : JsonValue::Object();
  o.Set("id", JsonValue::String(e.id));
  o.Set("kind", JsonValue::String(e.kind));
  o.Set("anchor", JsonValue::String(e.anchor));
  o.Set("x", JsonValue::Number(e.x));
  o.Set("y", JsonValue::Number(e.y));
  o.Set("w", JsonValue::Number(e.w));
  o.Set("h", JsonValue::Number(e.h));
  o.Set("slice_l", JsonValue::Number(e.slice_l));
  o.Set("slice_r", JsonValue::Number(e.slice_r));
  o.Set("slice_t", JsonValue::Number(e.slice_t));
  o.Set("slice_b", JsonValue::Number(e.slice_b));
  o.Set("slice_tile", JsonValue::Bool(e.slice_tile));
  o.Set("image", JsonValue::String(e.image));
  o.Set("src_x", JsonValue::Number(e.src_x));
  o.Set("src_y", JsonValue::Number(e.src_y));
  o.Set("src_w", JsonValue::Number(e.src_w));
  o.Set("src_h", JsonValue::Number(e.src_h));
  o.Set("frames", JsonValue::Number(e.frames));
  o.Set("frame", JsonValue::Number(e.frame));
  o.Set("text", JsonValue::String(e.text));
  o.Set("font_size", JsonValue::Number(e.font_size));
  o.Set("color", JsonValue::Number(static_cast<double>(e.color)));
  o.Set("align", JsonValue::Number(e.align));
  o.Set("value_var", JsonValue::String(e.value_var));
  o.Set("map", JsonValue::Number(e.map));
  o.Set("in_min", JsonValue::Number(e.in_min));
  o.Set("in_max", JsonValue::Number(e.in_max));
  o.Set("out_min", JsonValue::Number(e.out_min));
  o.Set("out_max", JsonValue::Number(e.out_max));
  o.Set("format", JsonValue::String(e.format));
  o.Set("visible_var", JsonValue::String(e.visible_var));
  o.Set("visible_value", JsonValue::Number(e.visible_value));
  o.Set("padding", JsonValue::Number(e.padding));
  JsonValue children = JsonValue::Array();
  for (const UiElementDoc& c : e.children) children.Push(WriteElement(c));
  o.Set("children", std::move(children));
  return o;
}

}  // namespace

bool UiScreenLoad(const std::filesystem::path& path, UiScreenDoc* out,
                  std::string* error) {
  if (out == nullptr) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "打不开 UI 文件：" + path.u8string();
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
  JsonValue root;
  if (!JsonParse(text, &root, error)) return false;
  if (!root.IsObject()) {
    if (error != nullptr) *error = "UI 文件不是对象";
    return false;
  }
  out->raw = root;
  ReadStr(root, "id", &out->id);
  ReadStr(root, "name", &out->name);
  ReadBool(root, "modal", &out->modal);
  ReadStr(root, "layout", &out->layout);
  const JsonValue* elements = root.Find("elements");
  if (elements != nullptr && elements->IsArray()) {
    for (const JsonValue& ev : elements->items) {
      UiElementDoc e;
      ReadElement(ev, &e);
      out->elements.push_back(std::move(e));
    }
  }
  if (out->id.empty()) out->id = path.stem().u8string();
  return true;
}

bool UiScreenSave(const std::filesystem::path& path, UiScreenDoc* doc,
                  std::string* error) {
  if (doc == nullptr) return false;
  JsonValue root = doc->raw.IsObject() ? doc->raw : JsonValue::Object();
  root.Set("id", JsonValue::String(doc->id));
  root.Set("name", JsonValue::String(doc->name));
  root.Set("modal", JsonValue::Bool(doc->modal));
  root.Set("layout", JsonValue::String(doc->layout));
  JsonValue elements = JsonValue::Array();
  for (const UiElementDoc& e : doc->elements) elements.Push(WriteElement(e));
  root.Set("elements", std::move(elements));
  doc->raw = root;
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (error != nullptr) *error = "写不了 UI 文件：" + path.u8string();
    return false;
  }
  const std::string text = JsonSerialize(root, true);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  return f.good();
}

}  // namespace pgmmv
