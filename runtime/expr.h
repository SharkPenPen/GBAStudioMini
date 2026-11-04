// runtime/expr.h
//
// 公式求值：数字、{属性}、{target.属性}、+ - * /、括号、
// min / max / clamp / abs / floor。故意做得很小——它不是脚本语言。
#pragma once

#include <cmath>
#include <map>
#include <string>

namespace r4 {

namespace expr_detail {

struct Parser {
  const std::string& s;
  size_t i = 0;
  const std::map<std::string, int>& self;
  const std::map<std::string, int>* target;
  bool ok = true;

  void Skip() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  }
  bool Eat(char c) {
    Skip();
    if (i < s.size() && s[i] == c) {
      ++i;
      return true;
    }
    return false;
  }
  std::string Ident() {
    Skip();
    size_t b = i;
    while (i < s.size() &&
           ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
            (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '.')) {
      ++i;
    }
    return s.substr(b, i - b);
  }
  int Lookup(const std::string& name) {
    if (name.rfind("target.", 0) == 0 && target != nullptr) {
      auto it = target->find(name.substr(7));
      return it == target->end() ? 0 : it->second;
    }
    if (name == "target.hp" && target != nullptr) {
      auto it = target->find("hp");
      return it == target->end() ? 0 : it->second;
    }
    auto it = self.find(name);
    return it == self.end() ? 0 : it->second;
  }
  int Expr();
  int Term();
  int Factor() {
    Skip();
    if (Eat('(')) {
      const int v = Expr();
      if (!Eat(')')) ok = false;
      return v;
    }
    if (Eat('-')) return -Factor();
    if (i < s.size() && s[i] == '{') {
      ++i;
      std::string name;
      while (i < s.size() && s[i] != '}') name.push_back(s[i++]);
      if (i < s.size()) ++i;
      return Lookup(name);
    }
    if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      int v = 0;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        ++i;
      }
      return v;
    }
    const std::string fn = Ident();
    if (fn.empty()) {
      ok = false;
      return 0;
    }
    if (!Eat('(')) {
      ok = false;
      return 0;
    }
    int a = Expr();
    int b = 0;
    int c = 0;
    bool two = false;
    bool three = false;
    if (Eat(',')) {
      b = Expr();
      two = true;
      if (Eat(',')) {
        c = Expr();
        three = true;
      }
    }
    if (!Eat(')')) ok = false;
    if (fn == "min") return two ? (a < b ? a : b) : a;
    if (fn == "max") return two ? (a > b ? a : b) : a;
    if (fn == "clamp" && three) return a < b ? b : (a > c ? c : a);
    if (fn == "abs") return a < 0 ? -a : a;
    if (fn == "floor") return a;
    ok = false;
    return 0;
  }
};

inline int Parser::Term() {
  int v = Factor();
  for (;;) {
    Skip();
    if (i < s.size() && s[i] == '*') {
      ++i;
      v *= Factor();
    } else if (i < s.size() && s[i] == '/') {
      ++i;
      const int d = Factor();
      v = d == 0 ? 0 : v / d;
    } else {
      return v;
    }
  }
}

inline int Parser::Expr() {
  int v = Term();
  for (;;) {
    Skip();
    if (i < s.size() && s[i] == '+') {
      ++i;
      v += Term();
    } else if (i < s.size() && s[i] == '-') {
      ++i;
      v -= Term();
    } else {
      return v;
    }
  }
}

}  // namespace expr_detail

inline int EvalExpr(const std::string& src,
                    const std::map<std::string, int>& self,
                    const std::map<std::string, int>* target, bool* ok = nullptr) {
  expr_detail::Parser p{src, 0, self, target};
  const int v = p.Expr();
  p.Skip();
  const bool good = p.ok && p.i >= src.size();
  if (ok != nullptr) *ok = good;
  return good ? v : 0;
}

}  // namespace r4
