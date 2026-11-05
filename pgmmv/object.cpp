// pgmmv/object.cpp
#include "pgmmv/object.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

namespace pgmmv {

namespace {

bool ReadFileText(const std::filesystem::path& path, std::string* out,
                  std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error != nullptr) *error = "cannot open object file: " + path.u8string();
    return false;
  }
  f.seekg(0, std::ios::end);
  const std::streamoff n = f.tellg();
  if (n < 0) {
    if (error != nullptr) *error = "cannot size object file: " + path.u8string();
    return false;
  }
  f.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(n));
  if (n > 0) {
    f.read(out->data(), n);
    if (!f) {
      if (error != nullptr) *error = "cannot read object file: " + path.u8string();
      return false;
    }
  }
  return true;
}

bool WriteFileText(const std::filesystem::path& path, const std::string& text,
                   std::string* error) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (error != nullptr) *error = "cannot write object file: " + path.u8string();
    return false;
  }
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!f) {
    if (error != nullptr) *error = "write failed: " + path.u8string();
    return false;
  }
  return true;
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
    if (error != nullptr) *error = std::string("object field is not int: ") + key;
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
    if (error != nullptr) *error = std::string("object field is not string: ") + key;
    return false;
  }
  *out = v->text;
  return true;
}

bool ReadBool(const JsonValue& obj, const char* key, bool* out, bool* found,
              std::string* error) {
  const JsonValue* v = obj.Find(key);
  if (found != nullptr) *found = v != nullptr;
  if (v == nullptr) return true;
  if (!v->IsBool()) {
    if (error != nullptr) *error = std::string("expected bool: ") + key;
    return false;
  }
  *out = v->boolean;
  return true;
}

void SetInt(JsonValue* obj, const char* key, int v) {
  obj->Set(key, JsonValue::Number(static_cast<double>(v)));
}

void SetBool(JsonValue* obj, const char* key, bool v) {
  obj->Set(key, JsonValue::Bool(v));
}

const char* CondName(RuleCond t) {
  switch (t) {
    case RuleCond::kAlways: return "always";
    case RuleCond::kKeyDown: return "key_down";
    case RuleCond::kKeyPressed: return "key_pressed";
    case RuleCond::kKeyUp: return "key_up";
    case RuleCond::kOnGround: return "on_ground";
    case RuleCond::kOnGroundNot: return "on_ground_not";
    case RuleCond::kAnimEnd: return "anim_end";
    case RuleCond::kAttrLe: return "attr_le";
    case RuleCond::kVarCmp: return "var_cmp";
    case RuleCond::kTimer: return "timer";
    case RuleCond::kPlayerDistLt: return "player_dist_lt";
    case RuleCond::kKeyHeld: return "key_held";
    case RuleCond::kPushable: return "pushable";
  }
  return "always";
}

bool CondFromName(const std::string& s, RuleCond* out) {
  if (s == "always") { *out = RuleCond::kAlways; return true; }
  if (s == "key_down") { *out = RuleCond::kKeyDown; return true; }
  if (s == "key_pressed") { *out = RuleCond::kKeyPressed; return true; }
  if (s == "key_up") { *out = RuleCond::kKeyUp; return true; }
  if (s == "on_ground") { *out = RuleCond::kOnGround; return true; }
  if (s == "on_ground_not") { *out = RuleCond::kOnGroundNot; return true; }
  if (s == "anim_end") { *out = RuleCond::kAnimEnd; return true; }
  if (s == "attr_le") { *out = RuleCond::kAttrLe; return true; }
  if (s == "var_cmp") { *out = RuleCond::kVarCmp; return true; }
  if (s == "timer") { *out = RuleCond::kTimer; return true; }
  if (s == "player_dist_lt") { *out = RuleCond::kPlayerDistLt; return true; }
  if (s == "key_held") { *out = RuleCond::kKeyHeld; return true; }
  if (s == "pushable") { *out = RuleCond::kPushable; return true; }
  return false;
}

const char* ActName(RuleAct t) {
  switch (t) {
    case RuleAct::kMove: return "move";
    case RuleAct::kMovePx: return "move_px";
    case RuleAct::kAnim: return "anim";
    case RuleAct::kFlip: return "flip";
    case RuleAct::kGravity: return "gravity";
    case RuleAct::kSfx: return "sfx";
    case RuleAct::kSpawn: return "spawn";
    case RuleAct::kDestroy: return "destroy";
    case RuleAct::kAttrAdd: return "attr_add";
    case RuleAct::kAttrSet: return "attr_set";
    case RuleAct::kDamage: return "damage";
    case RuleAct::kMoveAdd: return "move_add";
    case RuleAct::kMoveInput: return "move_input";
  }
  return "move";
}

bool ActFromName(const std::string& s, RuleAct* out) {
  if (s == "move") { *out = RuleAct::kMove; return true; }
  if (s == "move_px") { *out = RuleAct::kMovePx; return true; }
  if (s == "anim") { *out = RuleAct::kAnim; return true; }
  if (s == "flip") { *out = RuleAct::kFlip; return true; }
  if (s == "gravity") { *out = RuleAct::kGravity; return true; }
  if (s == "sfx") { *out = RuleAct::kSfx; return true; }
  if (s == "spawn") { *out = RuleAct::kSpawn; return true; }
  if (s == "destroy") { *out = RuleAct::kDestroy; return true; }
  if (s == "attr_add") { *out = RuleAct::kAttrAdd; return true; }
  if (s == "attr_set") { *out = RuleAct::kAttrSet; return true; }
  if (s == "damage") { *out = RuleAct::kDamage; return true; }
  if (s == "move_add") { *out = RuleAct::kMoveAdd; return true; }
  if (s == "move_input") { *out = RuleAct::kMoveInput; return true; }
  return false;
}

bool ReadRules(const JsonValue& root, ObjectDoc* o, std::string* error) {
  bool found = false;
  if (const JsonValue* attrs = root.Find("attrs");
      attrs != nullptr && attrs->IsArray()) {
    for (const JsonValue& av : attrs->items) {
      if (!av.IsObject()) continue;
      AttrDef a;
      if (!ReadString(av, "name", &a.name, &found, error)) return false;
      if (a.name.empty()) continue;
      int v = 0;
      if (!ReadInt(av, "value", &v, &found, error)) return false;
      a.value = v;
      o->attrs.push_back(std::move(a));
    }
  }
  if (const JsonValue* rules = root.Find("rules");
      rules != nullptr && rules->IsArray()) {
    for (const JsonValue& rv : rules->items) {
      if (!rv.IsObject()) continue;
      Rule r;
      if (!ReadString(rv, "name", &r.name, &found, error)) return false;
      if (!ReadBool(rv, "enabled", &r.enabled, &found, error)) return false;
      if (const JsonValue* when = rv.Find("when");
          when != nullptr && when->IsArray()) {
        for (const JsonValue& cv : when->items) {
          if (!cv.IsObject()) continue;
          Cond c;
          std::string tn;
          if (!ReadString(cv, "type", &tn, &found, error)) return false;
          if (!CondFromName(tn, &c.type)) {
            if (error != nullptr) *error = "unknown rule condition: " + tn;
            return false;
          }
          if (!ReadString(cv, "key", &c.key, &found, error)) return false;
          if (!ReadString(cv, "name", &c.name, &found, error)) return false;
          int v = 0;
          if (!ReadInt(cv, "op", &v, &found, error)) return false;
          if (found) c.op = v;
          if (!ReadInt(cv, "value", &v, &found, error)) return false;
          if (found) c.value = v;
          r.when.push_back(std::move(c));
        }
      }
      if (const JsonValue* ef = rv.Find("do");
          ef != nullptr && ef->IsArray()) {
        for (const JsonValue& ev : ef->items) {
          if (!ev.IsObject()) continue;
          Effect e;
          std::string tn;
          if (!ReadString(ev, "type", &tn, &found, error)) return false;
          if (!ActFromName(tn, &e.type)) {
            if (error != nullptr) *error = "unknown rule action: " + tn;
            return false;
          }
          int v = 0;
          if (!ReadInt(ev, "x", &v, &found, error)) return false;
          if (found) e.x = v;
          if (!ReadInt(ev, "y", &v, &found, error)) return false;
          if (found) e.y = v;
          if (!ReadString(ev, "text", &e.text, &found, error)) return false;
          if (!ReadString(ev, "formula", &e.formula, &found, error)) return false;
          r.effects.push_back(std::move(e));
        }
      }
      o->rules.push_back(std::move(r));
    }
  }
  return true;
}

void WriteRules(const ObjectDoc& o, JsonValue* root) {
  if (!o.attrs.empty()) {
    JsonValue arr = JsonValue::Array();
    for (const AttrDef& a : o.attrs) {
      JsonValue av = JsonValue::Object();
      av.Set("name", JsonValue::String(a.name));
      SetInt(&av, "value", a.value);
      arr.Push(std::move(av));
    }
    root->Set("attrs", std::move(arr));
  }
  if (o.rules.empty()) {
    root->Remove("rules");
    return;
  }
  JsonValue arr = JsonValue::Array();
  for (const Rule& r : o.rules) {
    JsonValue rv = JsonValue::Object();
    rv.Set("name", JsonValue::String(r.name));
    rv.Set("enabled", JsonValue::Bool(r.enabled));
    JsonValue when = JsonValue::Array();
    for (const Cond& c : r.when) {
      JsonValue cv = JsonValue::Object();
      cv.Set("type", JsonValue::String(CondName(c.type)));
      if (!c.key.empty()) cv.Set("key", JsonValue::String(c.key));
      if (!c.name.empty()) cv.Set("name", JsonValue::String(c.name));
      SetInt(&cv, "op", c.op);
      SetInt(&cv, "value", c.value);
      when.Push(std::move(cv));
    }
    rv.Set("when", std::move(when));
    JsonValue eff = JsonValue::Array();
    for (const Effect& e : r.effects) {
      JsonValue ev = JsonValue::Object();
      ev.Set("type", JsonValue::String(ActName(e.type)));
      SetInt(&ev, "x", e.x);
      SetInt(&ev, "y", e.y);
      if (!e.text.empty()) ev.Set("text", JsonValue::String(e.text));
      if (!e.formula.empty()) ev.Set("formula", JsonValue::String(e.formula));
      eff.Push(std::move(ev));
    }
    rv.Set("do", std::move(eff));
    arr.Push(std::move(rv));
  }
  root->Set("rules", std::move(arr));
}

// 盒子的 JSON：{ "x","y","w","h" } 或 { "none": true }；键不存在 = 沿用上一帧。
bool ReadFrameBox(const JsonValue& bv, FrameBox* out, std::string* error) {
  if (!bv.IsObject()) return true;
  const JsonValue* none = bv.Find("none");
  if (none != nullptr && none->IsBool() && none->boolean) {
    out->state = BoxState::kNone;
    return true;
  }
  bool found = false;
  int b = 0;
  CollisionBox box{0, 0, 0, 0};
  if (!ReadInt(bv, "x", &b, &found, error)) return false;
  box.x = b;
  if (!ReadInt(bv, "y", &b, &found, error)) return false;
  box.y = b;
  if (!ReadInt(bv, "w", &b, &found, error)) return false;
  box.w = std::max(0, b);
  if (!ReadInt(bv, "h", &b, &found, error)) return false;
  box.h = std::max(0, b);
  out->box = box;
  out->state = BoxState::kBox;
  return true;
}

void WriteFrameBox(JsonValue* fv, const char* key, const FrameBox& fb) {
  if (fb.state == BoxState::kInherit) return;
  JsonValue bv = JsonValue::Object();
  if (fb.state == BoxState::kNone) {
    bv.Set("none", JsonValue::Bool(true));
  } else {
    SetInt(&bv, "x", fb.box.x);
    SetInt(&bv, "y", fb.box.y);
    SetInt(&bv, "w", fb.box.w);
    SetInt(&bv, "h", fb.box.h);
  }
  fv->Set(key, std::move(bv));
}

bool ReadDirection(const JsonValue& v, DirectionDef* d, std::string* error) {
  bool found = false;
  int iv = 0;
  const JsonValue* frames = v.Find("frames");
  if (frames == nullptr || !frames->IsArray()) {
    if (error != nullptr) *error = "direction.frames missing";
    return false;
  }
  // frames 是对象数组：每帧一份完整数据（图 + hold + 两个盒子）
  for (const JsonValue& fv : frames->items) {
    if (!fv.IsObject()) {
      if (error != nullptr) *error = "direction.frames item must be object";
      return false;
    }
    ActionFrame f;
    if (!ReadInt(fv, "image", &f.image, &found, error) || !found) {
      if (error != nullptr) *error = "frame.image missing";
      return false;
    }
    if (!ReadInt(fv, "hold", &f.hold, &found, error)) return false;
    if (found) f.hold = std::max(0, std::min(600, f.hold));
    const JsonValue* org = fv.Find("origin");
    if (org != nullptr && org->IsObject()) {
      int o = 0;
      if (!ReadInt(*org, "x", &o, &found, error)) return false;
      f.origin.x = o;
      if (!ReadInt(*org, "y", &o, &found, error)) return false;
      f.origin.y = o;
      f.origin.set = true;
    }
    const JsonValue* col = fv.Find("collision");
    if (col != nullptr && !ReadFrameBox(*col, &f.collision, error)) return false;
    const JsonValue* atk = fv.Find("attack");
    if (atk != nullptr && !ReadFrameBox(*atk, &f.attack, error)) return false;
    const JsonValue* hurt = fv.Find("hurt");
    if (hurt != nullptr && !ReadFrameBox(*hurt, &f.hurt, error)) return false;
    const JsonValue* attach = fv.Find("attach");
    if (attach != nullptr) {
      if (!attach->IsArray()) {
        if (error != nullptr) *error = "frame.attach must be an array";
        return false;
      }
      for (const JsonValue& av : attach->items) {
        if (!av.IsObject()) continue;
        AttachPoint ap;
        if (!ReadString(av, "name", &ap.name, &found, error)) return false;
        if (ap.name.empty()) continue;
        int v = 0;
        if (!ReadInt(av, "x", &v, &found, error)) return false;
        ap.x = v;
        if (!ReadInt(av, "y", &v, &found, error)) return false;
        ap.y = v;
        f.attach.push_back(std::move(ap));
      }
    }
    d->frames.push_back(f);
  }
  if (!ReadInt(v, "fps", &iv, &found, error)) return false;
  if (found) d->fps = iv;
  if (!ReadBool(v, "loop", &d->loop, &found, error)) return false;
  const JsonValue* origin = v.Find("origin");
  if (origin != nullptr && origin->IsObject()) {
    int o = 0;
    if (!ReadInt(*origin, "x", &o, &found, error)) return false;
    if (found) d->origin_x = o;
    if (!ReadInt(*origin, "y", &o, &found, error)) return false;
    if (found) d->origin_y = o;
  }
  const JsonValue* offset = v.Find("offset");
  if (offset != nullptr && offset->IsObject()) {
    int o = 0;
    if (!ReadInt(*offset, "x", &o, &found, error)) return false;
    if (found) d->offset_x = o;
    if (!ReadInt(*offset, "y", &o, &found, error)) return false;
    if (found) d->offset_y = o;
  }
  if (!ReadBool(v, "flip_h", &d->flip_h, &found, error)) return false;
  if (!ReadBool(v, "flip_v", &d->flip_v, &found, error)) return false;
  return true;
}

JsonValue WriteDirection(const DirectionDef& d) {
  JsonValue v = JsonValue::Object();
  v.Set("name", JsonValue::String(d.name));
  JsonValue frames = JsonValue::Array();
  for (const ActionFrame& f : d.frames) {
    JsonValue fv = JsonValue::Object();
    SetInt(&fv, "image", f.image);
    SetInt(&fv, "hold", f.hold);
    if (f.origin.set) {
      JsonValue org = JsonValue::Object();
      SetInt(&org, "x", f.origin.x);
      SetInt(&org, "y", f.origin.y);
      fv.Set("origin", std::move(org));
    }
    WriteFrameBox(&fv, "collision", f.collision);
    WriteFrameBox(&fv, "attack", f.attack);
    WriteFrameBox(&fv, "hurt", f.hurt);
    if (!f.attach.empty()) {
      JsonValue arr = JsonValue::Array();
      for (const AttachPoint& ap : f.attach) {
        JsonValue pv = JsonValue::Object();
        pv.Set("name", JsonValue::String(ap.name));
        SetInt(&pv, "x", ap.x);
        SetInt(&pv, "y", ap.y);
        arr.Push(std::move(pv));
      }
      fv.Set("attach", std::move(arr));
    }
    frames.Push(std::move(fv));
  }
  v.Set("frames", std::move(frames));
  JsonValue origin = JsonValue::Object();
  SetInt(&origin, "x", d.origin_x);
  SetInt(&origin, "y", d.origin_y);
  v.Set("origin", std::move(origin));
  JsonValue offset = JsonValue::Object();
  SetInt(&offset, "x", d.offset_x);
  SetInt(&offset, "y", d.offset_y);
  v.Set("offset", std::move(offset));
  SetBool(&v, "flip_h", d.flip_h);
  SetBool(&v, "flip_v", d.flip_v);
  SetInt(&v, "fps", d.fps);
  SetBool(&v, "loop", d.loop);
  return v;
}


JsonValue ConditionToJson(const FlowCondition& c) {
  JsonValue o = JsonValue::Object();
  switch (c.type) {
    case ConditionType::kKey:
      o.Set("key", JsonValue::String(c.key));
      break;
    case ConditionType::kNotKey:
      o.Set("not_key", JsonValue::String(c.key));
      break;
    case ConditionType::kOnGround:
      o.Set("on_ground", JsonValue::Bool(true));
      break;
    case ConditionType::kNotOnGround:
      o.Set("not_on_ground", JsonValue::Bool(true));
      break;
    case ConditionType::kAnimEnd:
      o.Set("anim_end", JsonValue::Bool(true));
      break;
    case ConditionType::kHpLeZero:
      o.Set("hp_le_zero", JsonValue::Bool(true));
      break;
    case ConditionType::kAlways:
      o.Set("always", JsonValue::Bool(true));
      break;
    case ConditionType::kTimer:
      o.Set("timer", JsonValue::Number(static_cast<double>(c.value)));
      break;
    case ConditionType::kVariable:
      o.Set("variable", JsonValue::String(c.key));
      o.Set("value", JsonValue::Number(static_cast<double>(c.value)));
      break;
  }
  return o;
}

bool ParseCondition(const JsonValue& v, FlowCondition* out, std::string* error) {
  if (!v.IsObject()) {
    if (error != nullptr) *error = "flow condition must be an object";
    return false;
  }
  const JsonValue* key = v.Find("key");
  if (key != nullptr && key->IsString()) {
    out->type = ConditionType::kKey;
    out->key = key->text;
    return true;
  }
  const JsonValue* nkey = v.Find("not_key");
  if (nkey != nullptr && nkey->IsString()) {
    out->type = ConditionType::kNotKey;
    out->key = nkey->text;
    return true;
  }
  const JsonValue* og = v.Find("on_ground");
  if (og != nullptr) {
    out->type = ConditionType::kOnGround;
    return true;
  }
  const JsonValue* nog = v.Find("not_on_ground");
  if (nog != nullptr) {
    out->type = ConditionType::kNotOnGround;
    return true;
  }
  const JsonValue* ae = v.Find("anim_end");
  if (ae != nullptr) {
    out->type = ConditionType::kAnimEnd;
    return true;
  }
  const JsonValue* hp = v.Find("hp_le_zero");
  if (hp != nullptr) {
    out->type = ConditionType::kHpLeZero;
    return true;
  }
  const JsonValue* al = v.Find("always");
  if (al != nullptr) {
    out->type = ConditionType::kAlways;
    return true;
  }
  const JsonValue* tm = v.Find("timer");
  if (tm != nullptr && tm->IsNumber()) {
    out->type = ConditionType::kTimer;
    out->value = static_cast<int>(tm->number);
    return true;
  }
  const JsonValue* var = v.Find("variable");
  if (var != nullptr && var->IsString()) {
    const JsonValue* val = v.Find("value");
    out->type = ConditionType::kVariable;
    out->key = var->text;
    out->value = (val != nullptr && val->IsNumber()) ? static_cast<int>(val->number) : 0;
    return true;
  }
  if (error != nullptr) *error = "unsupported flow condition";
  return false;
}

}  // namespace

const char* ConditionTypeName(ConditionType t) {
  switch (t) {
    case ConditionType::kKey: return "key";
    case ConditionType::kNotKey: return "not_key";
    case ConditionType::kOnGround: return "on_ground";
    case ConditionType::kNotOnGround: return "not_on_ground";
    case ConditionType::kAnimEnd: return "anim_end";
    case ConditionType::kHpLeZero: return "hp_le_zero";
    case ConditionType::kAlways: return "always";
    case ConditionType::kTimer: return "timer";
    case ConditionType::kVariable: return "variable";
  }
  return "key";
}

bool ConditionTypeFromName(const std::string& name, ConditionType* out) {
  if (name == "key") { *out = ConditionType::kKey; return true; }
  if (name == "not_key") { *out = ConditionType::kNotKey; return true; }
  if (name == "on_ground") { *out = ConditionType::kOnGround; return true; }
  if (name == "not_on_ground") { *out = ConditionType::kNotOnGround; return true; }
  if (name == "anim_end") { *out = ConditionType::kAnimEnd; return true; }
  if (name == "hp_le_zero") { *out = ConditionType::kHpLeZero; return true; }
  if (name == "always") { *out = ConditionType::kAlways; return true; }
  if (name == "timer") { *out = ConditionType::kTimer; return true; }
  if (name == "variable") { *out = ConditionType::kVariable; return true; }
  return false;
}

std::string SanitizeObjectId(const std::string& name) {
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
  if (out.empty()) out = "object";
  return out;
}

ObjectDoc ObjectCreateDefault(const std::string& id, const std::string& name) {
  ObjectDoc o;
  o.id = id;
  o.name = name.empty() ? id : name;
  o.type = "player";
  o.frame = ObjectFrame{32, 32};
  // 默认锚点 = 脚下中心：原点在帧底边中点，碰撞盒以脚下为原点向上铺满整帧
  o.collision = CollisionBox{-16, -32, 32, 32};
  o.physics = ObjectPhysics{110, 900, false};

  // 动作不在这里建：新建对象时由调用方配一个动画资源（见 AnimationCreateForObject）

  o.default_action = "idle";
  FlowCondition right;
  right.type = ConditionType::kKey;
  right.key = "right";
  FlowCondition not_right;
  not_right.type = ConditionType::kNotKey;
  not_right.key = "right";
  FlowCondition left;
  left.type = ConditionType::kKey;
  left.key = "left";
  FlowCondition not_left;
  not_left.type = ConditionType::kNotKey;
  not_left.key = "left";
  FlowCondition jump;
  jump.type = ConditionType::kKey;
  jump.key = "jump";
  FlowCondition anim_end;
  anim_end.type = ConditionType::kAnimEnd;

  o.links.push_back(FlowLink{"idle", "run", {right}});
  o.links.push_back(FlowLink{"idle", "run", {left}});
  o.links.push_back(FlowLink{"run", "idle", {not_right, not_left}});
  o.links.push_back(FlowLink{"idle", "jump", {jump}});
  o.links.push_back(FlowLink{"jump", "idle", {anim_end}});
  return o;
}

bool ObjectLoad(const std::filesystem::path& path, ObjectDoc* out,
                std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) *error = "ObjectLoad: null output";
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
    if (error != nullptr) *error = "object JSON root must be an object";
    return false;
  }

  ObjectDoc o;
  o.raw = root;
  const JsonValue* fv = root.Find("format_version");
  if (fv != nullptr) {
    int v = 0;
    if (!ToInt(*fv, &v) || v < 1 || v > kObjectFormatVersion) {
      if (error != nullptr) *error = "object format_version unsupported";
      return false;
    }
    o.format_version = v;
  }

  bool found = false;
  if (!ReadString(root, "id", &o.id, &found, error)) return false;
  if (o.id.empty()) o.id = path.stem().u8string();
  if (!ReadString(root, "name", &o.name, &found, error)) return false;
  if (o.name.empty()) o.name = o.id;
  if (!ReadString(root, "type", &o.type, &found, error)) return false;
  if (!ReadString(root, "image", &o.image, &found, error)) return false;

  const JsonValue* frame = root.Find("frame");
  if (frame != nullptr) {
    if (!frame->IsObject()) {
      if (error != nullptr) *error = "object.frame must be an object";
      return false;
    }
    int v = 0;
    if (!ReadInt(*frame, "w", &v, &found, error)) return false;
    if (found) o.frame.w = v;
    if (!ReadInt(*frame, "h", &v, &found, error)) return false;
    if (found) o.frame.h = v;
  }

  const JsonValue* col = root.Find("collision");
  if (col != nullptr) {
    if (!col->IsObject()) {
      if (error != nullptr) *error = "object.collision must be an object";
      return false;
    }
    int v = 0;
    if (!ReadInt(*col, "x", &v, &found, error)) return false;
    if (found) o.collision.x = v;
    if (!ReadInt(*col, "y", &v, &found, error)) return false;
    if (found) o.collision.y = v;
    if (!ReadInt(*col, "w", &v, &found, error)) return false;
    if (found) o.collision.w = v;
    if (!ReadInt(*col, "h", &v, &found, error)) return false;
    if (found) o.collision.h = v;
  }

  if (!ReadString(root, "team", &o.team, &found, error)) return false;
  if (!ReadBool(root, "pushable", &o.pushable, &found, error)) return false;
  const JsonValue* hurt = root.Find("hurt");
  if (hurt != nullptr && hurt->IsObject()) {
    int v = 0;
    if (!ReadInt(*hurt, "x", &v, &found, error)) return false;
    if (found) o.hurt_box.x = v;
    if (!ReadInt(*hurt, "y", &v, &found, error)) return false;
    if (found) o.hurt_box.y = v;
    if (!ReadInt(*hurt, "w", &v, &found, error)) return false;
    if (found) o.hurt_box.w = v;
    if (!ReadInt(*hurt, "h", &v, &found, error)) return false;
    if (found) o.hurt_box.h = v;
  }

  const JsonValue* atk = root.Find("attack");
  if (atk != nullptr && atk->IsObject()) {
    int v = 0;
    if (!ReadInt(*atk, "x", &v, &found, error)) return false;
    if (found) o.attack_box.x = v;
    if (!ReadInt(*atk, "y", &v, &found, error)) return false;
    if (found) o.attack_box.y = v;
    if (!ReadInt(*atk, "w", &v, &found, error)) return false;
    if (found) o.attack_box.w = v;
    if (!ReadInt(*atk, "h", &v, &found, error)) return false;
    if (found) o.attack_box.h = v;
  }
  const JsonValue* phys = root.Find("physics");
  if (phys != nullptr) {
    if (!phys->IsObject()) {
      if (error != nullptr) *error = "object.physics must be an object";
      return false;
    }
    int v = 0;
    if (!ReadInt(*phys, "speed", &v, &found, error)) return false;
    if (found) o.physics.speed = v;
    if (!ReadInt(*phys, "gravity", &v, &found, error)) return false;
    if (found) o.physics.gravity = v;
    if (!ReadBool(*phys, "limit_screen", &o.physics.limit_screen, &found, error)) {
      return false;
    }
  }

  if (!ReadRules(root, &o, error)) return false;
  root.Remove("anim_left");
  root.Remove("anim_right");
  root.Remove("anim_jump");
  root.Remove("anim_fall");
  root.Remove("anim_idle");
  root.Remove("anim_attack");
  root.Remove("anim_run");
  root.Remove("run_hold_ms");
  root.Remove("run_speed_pct");
  root.Remove("behavior");
  root.Remove("patrol_range");
  root.Remove("dive_interval");
  // hp 只是属性表里的一条（没有就补默认值，规则/UI 都按属性读）
  bool has_hp = false;
  for (const AttrDef& a : o.attrs) {
    if (a.name == "hp") has_hp = true;
  }
  if (!has_hp) o.attrs.push_back(AttrDef{"hp", 100});
  if (!ReadString(root, "animation", &o.animation, &found, error)) return false;

  const JsonValue* spawners = root.Find("spawners");
  if (spawners != nullptr && spawners->IsArray()) {
    for (const JsonValue& sv : spawners->items) {
      if (!sv.IsObject()) continue;
      SpawnerDef sp;
      if (!ReadString(sv, "object", &sp.object, &found, error)) return false;
      int v = 0;
      if (!ReadInt(sv, "offset_x", &v, &found, error)) return false;
      if (found) sp.offset_x = v;
      if (!ReadInt(sv, "offset_y", &v, &found, error)) return false;
      if (found) sp.offset_y = v;
      const JsonValue* vx = sv.Find("vx");
      if (vx != nullptr && vx->IsNumber()) sp.vx = static_cast<float>(vx->number);
      const JsonValue* vy = sv.Find("vy");
      if (vy != nullptr && vy->IsNumber()) sp.vy = static_cast<float>(vy->number);
      if (!ReadInt(sv, "lifetime", &v, &found, error)) return false;
      if (found) sp.lifetime = v;
      if (!ReadInt(sv, "count", &v, &found, error)) return false;
      if (found) sp.count = v;
      if (!ReadInt(sv, "spread", &v, &found, error)) return false;
      if (found) sp.spread = v;
      if (!ReadBool(sv, "inherit_velocity", &sp.inherit_velocity, &found, error)) return false;
      if (!ReadBool(sv, "destroy_on_hit", &sp.destroy_on_hit, &found, error)) return false;
      if (!ReadBool(sv, "destroy_with_parent", &sp.destroy_with_parent, &found, error)) return false;
      o.spawners.push_back(std::move(sp));
    }
  }

  const JsonValue* flow = root.Find("flow");
  if (flow != nullptr) {
    if (!flow->IsObject()) {
      if (error != nullptr) *error = "object.flow must be an object";
      return false;
    }
    if (!ReadString(*flow, "default", &o.default_action, &found, error)) return false;
    const JsonValue* links = flow->Find("links");
    if (links != nullptr) {
      if (!links->IsArray()) {
        if (error != nullptr) *error = "flow.links must be an array";
        return false;
      }
      for (const JsonValue& lv : links->items) {
        if (!lv.IsObject()) {
          if (error != nullptr) *error = "flow link must be an object";
          return false;
        }
        FlowLink link;
        bool has = false;
        if (!ReadString(lv, "from", &link.from, &has, error) || !has ||
            link.from.empty()) {
          if (error != nullptr) *error = "flow link.from is required";
          return false;
        }
        if (!ReadString(lv, "to", &link.to, &has, error) || !has ||
            link.to.empty()) {
          if (error != nullptr) *error = "flow link.to is required";
          return false;
        }
        const JsonValue* conds = lv.Find("if");
        if (conds != nullptr) {
          if (!conds->IsArray()) {
            if (error != nullptr) *error = "flow link.if must be an array";
            return false;
          }
          for (const JsonValue& cv : conds->items) {
            FlowCondition c;
            if (!ParseCondition(cv, &c, error)) return false;
            link.conditions.push_back(std::move(c));
          }
        }
        o.links.push_back(std::move(link));
      }
    }
  }
  if (o.default_action.empty()) o.default_action = "idle";

  o.raw = root;
  *out = std::move(o);
  return true;
}

void ObjectSyncRaw(ObjectDoc* o) {
  if (o == nullptr) return;
  JsonValue root = o->raw.IsObject() ? o->raw : JsonValue::Object();
  root.Set("format_version", JsonValue::Number(static_cast<double>(o->format_version)));
  root.Set("id", JsonValue::String(o->id));
  root.Set("name", JsonValue::String(o->name));
  root.Set("type", JsonValue::String(o->type));
  root.Set("image", JsonValue::String(o->image));

  JsonValue frame = JsonValue::Object();
  SetInt(&frame, "w", o->frame.w);
  SetInt(&frame, "h", o->frame.h);
  root.Set("frame", std::move(frame));

  JsonValue col = JsonValue::Object();
  SetInt(&col, "x", o->collision.x);
  SetInt(&col, "y", o->collision.y);
  SetInt(&col, "w", o->collision.w);
  SetInt(&col, "h", o->collision.h);
  root.Set("collision", std::move(col));

  JsonValue hurt = JsonValue::Object();
  SetInt(&hurt, "x", o->hurt_box.x);
  SetInt(&hurt, "y", o->hurt_box.y);
  SetInt(&hurt, "w", o->hurt_box.w);
  SetInt(&hurt, "h", o->hurt_box.h);
  root.Set("hurt", std::move(hurt));

  JsonValue atk = JsonValue::Object();
  SetInt(&atk, "x", o->attack_box.x);
  SetInt(&atk, "y", o->attack_box.y);
  SetInt(&atk, "w", o->attack_box.w);
  SetInt(&atk, "h", o->attack_box.h);
  root.Set("attack", std::move(atk));
  root.Remove("attack_channel");
  root.Remove("hurt_channels");
  root.Remove("invincibility");
  if (!o->team.empty()) root.Set("team", JsonValue::String(o->team));
  if (o->pushable) root.Set("pushable", JsonValue::Bool(true));

  JsonValue phys = JsonValue::Object();
  SetInt(&phys, "speed", o->physics.speed);
  SetInt(&phys, "gravity", o->physics.gravity);
  phys.Set("limit_screen", JsonValue::Bool(o->physics.limit_screen));
  root.Set("physics", std::move(phys));

  root.Remove("hp");  // hp 现在只是属性表里的一条
  root.Remove("fire_interval");  // 射击参数都进属性表了
  root.Remove("bullet_speed");
  WriteRules(*o, &root);

  // 动作在动画资源文件里，对象不再写 actions
  root.Remove("actions");
  if (!o->animation.empty()) {
    root.Set("animation", JsonValue::String(o->animation));
  }
  if (o->animation.empty()) {
    for (auto it = root.fields.begin(); it != root.fields.end(); ++it) {
      if (it->first == "animation") {
        root.fields.erase(it);
        break;
      }
    }
  }

  JsonValue spawners = JsonValue::Array();
  for (const SpawnerDef& sp : o->spawners) {
    JsonValue sv = JsonValue::Object();
    sv.Set("object", JsonValue::String(sp.object));
    SetInt(&sv, "offset_x", sp.offset_x);
    SetInt(&sv, "offset_y", sp.offset_y);
    sv.Set("vx", JsonValue::Number(static_cast<double>(sp.vx)));
    sv.Set("vy", JsonValue::Number(static_cast<double>(sp.vy)));
    SetInt(&sv, "lifetime", sp.lifetime);
    SetInt(&sv, "count", sp.count);
    SetInt(&sv, "spread", sp.spread);
    SetBool(&sv, "inherit_velocity", sp.inherit_velocity);
    SetBool(&sv, "destroy_on_hit", sp.destroy_on_hit);
    SetBool(&sv, "destroy_with_parent", sp.destroy_with_parent);
    spawners.Push(std::move(sv));
  }
  root.Set("spawners", std::move(spawners));

  JsonValue flow =
      (root.Find("flow") != nullptr && root.Find("flow")->IsObject())
          ? *root.Find("flow")
          : JsonValue::Object();
  flow.Set("default", JsonValue::String(o->default_action));
  JsonValue links = JsonValue::Array();
  for (const FlowLink& link : o->links) {
    JsonValue lv = JsonValue::Object();
    lv.Set("from", JsonValue::String(link.from));
    lv.Set("to", JsonValue::String(link.to));
    JsonValue conds = JsonValue::Array();
    for (const FlowCondition& c : link.conditions) {
      conds.Push(ConditionToJson(c));
    }
    lv.Set("if", std::move(conds));
    links.Push(std::move(lv));
  }
  flow.Set("links", std::move(links));
  root.Set("flow", std::move(flow));

  o->raw = std::move(root);
}

bool ObjectSave(const std::filesystem::path& path, ObjectDoc* obj,
                std::string* error) {
  if (obj == nullptr || obj->id.empty()) {
    if (error != nullptr) *error = "ObjectSave: empty id";
    return false;
  }
  ObjectSyncRaw(obj);
  const std::string text = JsonSerialize(obj->raw, true) + "\n";
  return WriteFileText(path, text, error);
}


bool ReadDirectionForResource(const JsonValue& v, DirectionDef* d,
                              std::string* error) {
  return ReadDirection(v, d, error);
}

JsonValue WriteDirectionForResource(const DirectionDef& d) {
  return WriteDirection(d);
}

}  // namespace pgmmv
