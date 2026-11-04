// pgmmv/json.h
//
// 小型 JSON DOM（编辑器/工具侧使用；运行时不解 JSON）。
//
// 为什么自己写：
//   - 项目只允许最小依赖，不引第三方 JSON 库；
//   - 需要保留未知字段（前向兼容），所以需要一个可修改的 DOM，
//     而不是一次性的 typed parser；
//   - 不使用异常/RTTI（顶层 CMake 已关），解析失败用 bool + error 字符串。
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pgmmv {

enum class JsonType {
  kNull = 0,
  kBool,
  kNumber,
  kString,
  kArray,
  kObject,
};

struct JsonValue {
  JsonType type = JsonType::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string text;
  std::vector<JsonValue> items;
  std::vector<std::pair<std::string, JsonValue>> fields;

  static JsonValue Null();
  static JsonValue Bool(bool v);
  static JsonValue Number(double v);
  static JsonValue String(std::string v);
  static JsonValue Array();
  static JsonValue Object();

  bool IsNull() const { return type == JsonType::kNull; }
  bool IsBool() const { return type == JsonType::kBool; }
  bool IsNumber() const { return type == JsonType::kNumber; }
  bool IsString() const { return type == JsonType::kString; }
  bool IsArray() const { return type == JsonType::kArray; }
  bool IsObject() const { return type == JsonType::kObject; }

  const JsonValue* Find(const char* key) const;
  JsonValue* Find(const char* key);
  const JsonValue* Find(const std::string& key) const;
  JsonValue* Find(const std::string& key);

  // 仅对 object 有意义；不存在则追加，存在则替换值（保留原顺序）。
  void Set(const std::string& key, JsonValue value);
  // 仅对 object 有意义；删掉这个键（返回是否删掉了）。
  bool Remove(const std::string& key);
  // 仅对 array 有意义。
  void Push(JsonValue value);
};

// 失败时返回 false，并把原因写进 error（含字节位置）。
bool JsonParse(const std::string& text, JsonValue* out, std::string* error);

// pretty=true 时缩进两格；字符串按 JSON 规则转义，UTF-8 原样保留。
std::string JsonSerialize(const JsonValue& value, bool pretty = true);

}  // namespace pgmmv
