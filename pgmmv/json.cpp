// pgmmv/json.cpp
#include "pgmmv/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pgmmv {

namespace {

constexpr int kMaxDepth = 64;

void AppendUtf8(unsigned int cp, std::string* out) {
  if (cp <= 0x7Fu) {
    out->push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out->push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out->push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out->push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out->push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  bool Parse(JsonValue* out, std::string* error) {
    // 允许 UTF-8 BOM（Windows 记事本可能加）
    if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
        static_cast<unsigned char>(text_[1]) == 0xBB &&
        static_cast<unsigned char>(text_[2]) == 0xBF) {
      pos_ = 3;
    }
    SkipWs();
    if (!ParseValue(out, 0)) {
      if (error != nullptr) {
        *error = error_;
      }
      return false;
    }
    SkipWs();
    if (pos_ != text_.size()) {
      Fail("trailing characters");
      if (error != nullptr) {
        *error = error_;
      }
      return false;
    }
    return true;
  }

 private:
  bool Fail(const char* what) {
    if (error_.empty()) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "JSON error at byte %zu: %s", pos_, what);
      error_ = buf;
    }
    return false;
  }

  void SkipWs() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool ParseValue(JsonValue* out, int depth) {
    if (depth > kMaxDepth) {
      return Fail("nesting too deep");
    }
    SkipWs();
    if (pos_ >= text_.size()) {
      return Fail("unexpected end");
    }
    const char c = text_[pos_];
    if (c == '{') {
      return ParseObject(out, depth);
    }
    if (c == '[') {
      return ParseArray(out, depth);
    }
    if (c == '"') {
      std::string s;
      if (!ParseString(&s)) {
        return false;
      }
      *out = JsonValue::String(std::move(s));
      return true;
    }
    if (c == 't') {
      if (!ConsumeLiteral("true")) {
        return false;
      }
      *out = JsonValue::Bool(true);
      return true;
    }
    if (c == 'f') {
      if (!ConsumeLiteral("false")) {
        return false;
      }
      *out = JsonValue::Bool(false);
      return true;
    }
    if (c == 'n') {
      if (!ConsumeLiteral("null")) {
        return false;
      }
      *out = JsonValue::Null();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      double v = 0.0;
      if (!ParseNumber(&v)) {
        return false;
      }
      *out = JsonValue::Number(v);
      return true;
    }
    return Fail("unexpected character");
  }

  bool ParseObject(JsonValue* out, int depth) {
    ++pos_;  // {
    JsonValue obj = JsonValue::Object();
    SkipWs();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      *out = std::move(obj);
      return true;
    }
    while (true) {
      SkipWs();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        return Fail("object key must be a string");
      }
      std::string key;
      if (!ParseString(&key)) {
        return false;
      }
      SkipWs();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return Fail("expected ':' after key");
      }
      ++pos_;
      JsonValue value;
      if (!ParseValue(&value, depth + 1)) {
        return false;
      }
      obj.Set(key, std::move(value));
      SkipWs();
      if (pos_ >= text_.size()) {
        return Fail("unterminated object");
      }
      const char c = text_[pos_];
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == '}') {
        ++pos_;
        *out = std::move(obj);
        return true;
      }
      return Fail("expected ',' or '}'");
    }
  }

  bool ParseArray(JsonValue* out, int depth) {
    ++pos_;  // [
    JsonValue arr = JsonValue::Array();
    SkipWs();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      *out = std::move(arr);
      return true;
    }
    while (true) {
      JsonValue value;
      if (!ParseValue(&value, depth + 1)) {
        return false;
      }
      arr.Push(std::move(value));
      SkipWs();
      if (pos_ >= text_.size()) {
        return Fail("unterminated array");
      }
      const char c = text_[pos_];
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == ']') {
        ++pos_;
        *out = std::move(arr);
        return true;
      }
      return Fail("expected ',' or ']'");
    }
  }

  bool ParseString(std::string* out) {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      return Fail("expected string");
    }
    ++pos_;
    out->clear();
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c < 0x20u) {
        return Fail("control character in string");
      }
      if (c != '\\') {
        out->push_back(static_cast<char>(c));
        ++pos_;
        continue;
      }
      ++pos_;  // backslash
      if (pos_ >= text_.size()) {
        return Fail("unterminated escape");
      }
      const char e = text_[pos_++];
      switch (e) {
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case '/': out->push_back('/'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        case 'u': {
          unsigned int cp = 0;
          if (!ParseHex4(&cp)) {
            return false;
          }
          if (cp >= 0xD800u && cp <= 0xDBFFu) {
            if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
              return Fail("missing low surrogate");
            }
            pos_ += 2;
            unsigned int low = 0;
            if (!ParseHex4(&low) || low < 0xDC00u || low > 0xDFFFu) {
              return Fail("invalid low surrogate");
            }
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
          } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return Fail("unexpected low surrogate");
          }
          AppendUtf8(cp, out);
          break;
        }
        default:
          return Fail("invalid escape");
      }
    }
    return Fail("unterminated string");
  }

  bool ParseHex4(unsigned int* out) {
    if (pos_ + 4 > text_.size()) {
      return Fail("short unicode escape");
    }
    unsigned int v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= static_cast<unsigned int>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= static_cast<unsigned int>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= static_cast<unsigned int>(c - 'A' + 10);
      } else {
        return Fail("invalid hex digit");
      }
    }
    *out = v;
    return true;
  }

  bool ConsumeLiteral(const char* lit) {
    const size_t n = std::strlen(lit);
    if (pos_ + n > text_.size() || std::memcmp(text_.data() + pos_, lit, n) != 0) {
      return Fail("invalid literal");
    }
    pos_ += n;
    return true;
  }

  bool ParseNumber(double* out) {
    const size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= text_.size()) {
      return Fail("unterminated number");
    }
    if (text_[pos_] == '0') {
      ++pos_;
      if (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        return Fail("leading zero in number");
      }
    } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    } else {
      return Fail("invalid number");
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        return Fail("missing fraction digits");
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        return Fail("missing exponent digits");
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    const std::string num = text_.substr(start, pos_ - start);
    char* end = nullptr;
    const double v = std::strtod(num.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(v)) {
      return Fail("number parse failed");
    }
    *out = v;
    return true;
  }

  const std::string& text_;
  size_t pos_ = 0;
  std::string error_;
};

void EscapeString(const std::string& s, std::string* out) {
  out->push_back('"');
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': out->append("\\\""); break;
      case '\\': out->append("\\\\"); break;
      case '\b': out->append("\\b"); break;
      case '\f': out->append("\\f"); break;
      case '\n': out->append("\\n"); break;
      case '\r': out->append("\\r"); break;
      case '\t': out->append("\\t"); break;
      default:
        if (c < 0x20u) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned int>(c));
          out->append(buf);
        } else {
          out->push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out->push_back('"');
}

void Indent(std::string* out, int level) {
  for (int i = 0; i < level; ++i) {
    out->append("  ");
  }
}

bool AllScalars(const JsonValue& v) {
  for (const JsonValue& item : v.items) {
    if (item.type == JsonType::kArray || item.type == JsonType::kObject) {
      return false;
    }
  }
  return true;
}

void WriteValue(const JsonValue& v, int level, bool pretty, std::string* out) {
  switch (v.type) {
    case JsonType::kNull:
      out->append("null");
      break;
    case JsonType::kBool:
      out->append(v.boolean ? "true" : "false");
      break;
    case JsonType::kNumber: {
      char buf[64];
      const double d = v.number;
      if (std::isfinite(d) && d >= -9.0e15 && d <= 9.0e15 &&
          d == static_cast<double>(static_cast<long long>(d))) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
      } else {
        std::snprintf(buf, sizeof(buf), "%.17g", d);
      }
      out->append(buf);
      break;
    }
    case JsonType::kString:
      EscapeString(v.text, out);
      break;
    case JsonType::kArray: {
      out->push_back('[');
      if (v.items.empty()) {
        out->push_back(']');
        break;
      }
      // 标量数组（如 screen 的 [400, 240]）保持一行，便于人读
      if (pretty && AllScalars(v)) {
        for (size_t i = 0; i < v.items.size(); ++i) {
          if (i > 0) {
            out->append(", ");
          }
          WriteValue(v.items[i], 0, false, out);
        }
        out->push_back(']');
        break;
      }
      if (pretty) {
        out->push_back('\n');
      }
      for (size_t i = 0; i < v.items.size(); ++i) {
        if (pretty) {
          Indent(out, level + 1);
        }
        WriteValue(v.items[i], level + 1, pretty, out);
        if (i + 1 < v.items.size()) {
          out->push_back(',');
        }
        if (pretty) {
          out->push_back('\n');
        }
      }
      if (pretty) {
        Indent(out, level);
      }
      out->push_back(']');
      break;
    }
    case JsonType::kObject: {
      out->push_back('{');
      if (v.fields.empty()) {
        out->push_back('}');
        break;
      }
      if (pretty) {
        out->push_back('\n');
      }
      for (size_t i = 0; i < v.fields.size(); ++i) {
        if (pretty) {
          Indent(out, level + 1);
        }
        EscapeString(v.fields[i].first, out);
        out->append(pretty ? ": " : ":");
        WriteValue(v.fields[i].second, level + 1, pretty, out);
        if (i + 1 < v.fields.size()) {
          out->push_back(',');
        }
        if (pretty) {
          out->push_back('\n');
        }
      }
      if (pretty) {
        Indent(out, level);
      }
      out->push_back('}');
      break;
    }
  }
}

}  // namespace

JsonValue JsonValue::Null() { return JsonValue(); }

JsonValue JsonValue::Bool(bool v) {
  JsonValue j;
  j.type = JsonType::kBool;
  j.boolean = v;
  return j;
}

JsonValue JsonValue::Number(double v) {
  JsonValue j;
  j.type = JsonType::kNumber;
  j.number = v;
  return j;
}

JsonValue JsonValue::String(std::string v) {
  JsonValue j;
  j.type = JsonType::kString;
  j.text = std::move(v);
  return j;
}

JsonValue JsonValue::Array() {
  JsonValue j;
  j.type = JsonType::kArray;
  return j;
}

JsonValue JsonValue::Object() {
  JsonValue j;
  j.type = JsonType::kObject;
  return j;
}

const JsonValue* JsonValue::Find(const char* key) const { return Find(std::string(key)); }
JsonValue* JsonValue::Find(const char* key) { return Find(std::string(key)); }

const JsonValue* JsonValue::Find(const std::string& key) const {
  if (type != JsonType::kObject) {
    return nullptr;
  }
  for (const auto& f : fields) {
    if (f.first == key) {
      return &f.second;
    }
  }
  return nullptr;
}

JsonValue* JsonValue::Find(const std::string& key) {
  if (type != JsonType::kObject) {
    return nullptr;
  }
  for (auto& f : fields) {
    if (f.first == key) {
      return &f.second;
    }
  }
  return nullptr;
}

void JsonValue::Set(const std::string& key, JsonValue value) {
  if (type != JsonType::kObject) {
    type = JsonType::kObject;
    fields.clear();
  }
  for (auto& f : fields) {
    if (f.first == key) {
      f.second = std::move(value);
      return;
    }
  }
  fields.emplace_back(key, std::move(value));
}

bool JsonValue::Remove(const std::string& key) {
  if (type != JsonType::kObject) return false;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i].first == key) {
      fields.erase(fields.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

void JsonValue::Push(JsonValue value) {
  if (type != JsonType::kArray) {
    type = JsonType::kArray;
    items.clear();
  }
  items.push_back(std::move(value));
}

bool JsonParse(const std::string& text, JsonValue* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "JSON error: output is null";
    }
    return false;
  }
  Parser parser(text);
  return parser.Parse(out, error);
}

std::string JsonSerialize(const JsonValue& value, bool pretty) {
  std::string out;
  WriteValue(value, 0, pretty, &out);
  return out;
}

}  // namespace pgmmv
