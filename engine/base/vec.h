// engine/base/vec.h
// 标量向量/矩阵；浮点 == 只用于同一性判断，比较用 NearlyEquals。
#pragma once

#include "base/math.h"
#include "base/types.h"

namespace eng {

// ===========================================================================
// Vec2
// ===========================================================================
struct Vec2 {
  f32 x = 0.0f;
  f32 y = 0.0f;
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(Vec2 a) { return Vec2{-a.x, -a.y}; }
constexpr Vec2 operator*(Vec2 a, f32 s) { return Vec2{a.x * s, a.y * s}; }
constexpr Vec2 operator*(f32 s, Vec2 a) { return Vec2{a.x * s, a.y * s}; }
constexpr Vec2 operator/(Vec2 a, f32 s) { return Vec2{a.x / s, a.y / s}; }

constexpr Vec2& operator+=(Vec2& a, Vec2 b) { a = a + b; return a; }
constexpr Vec2& operator-=(Vec2& a, Vec2 b) { a = a - b; return a; }
constexpr Vec2& operator*=(Vec2& a, f32 s) { a = a * s; return a; }

constexpr bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }

constexpr f32 Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr f32 LengthSq(Vec2 a) { return Dot(a, a); }
inline    f32 Length(Vec2 a) { return Sqrt(LengthSq(a)); }
inline    Vec2 Normalized(Vec2 a) {
  const f32 len = Length(a);
  return len > 0.0f ? a / len : Vec2{};
}
// 垂直于 a 的向量（2D 常用：法线）
constexpr Vec2 Perp(Vec2 a) { return Vec2{-a.y, a.x}; }
constexpr Vec2 Lerp(Vec2 a, Vec2 b, f32 t) { return Vec2{Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)}; }
inline bool NearlyEquals(Vec2 a, Vec2 b, f32 eps = kEpsilon) {
  return NearEq(a.x, b.x, eps) && NearEq(a.y, b.y, eps);
}

// ===========================================================================
// Vec3
// ===========================================================================
struct Vec3 {
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 a) { return Vec3{-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, f32 s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(f32 s, Vec3 a) { return Vec3{a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator/(Vec3 a, f32 s) { return Vec3{a.x / s, a.y / s, a.z / s}; }

constexpr Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
constexpr Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
constexpr Vec3& operator*=(Vec3& a, f32 s) { a = a * s; return a; }

constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
constexpr bool operator!=(Vec3 a, Vec3 b) { return !(a == b); }

constexpr f32 Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 Cross(Vec3 a, Vec3 b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr f32 LengthSq(Vec3 a) { return Dot(a, a); }
inline    f32 Length(Vec3 a) { return Sqrt(LengthSq(a)); }
inline    Vec3 Normalized(Vec3 a) {
  const f32 len = Length(a);
  return len > 0.0f ? a / len : Vec3{};
}
constexpr Vec3 Lerp(Vec3 a, Vec3 b, f32 t) {
  return Vec3{Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}
inline bool NearlyEquals(Vec3 a, Vec3 b, f32 eps = kEpsilon) {
  return NearEq(a.x, b.x, eps) && NearEq(a.y, b.y, eps) && NearEq(a.z, b.z, eps);
}

// ===========================================================================
// Vec4
// ===========================================================================
struct Vec4 {
  f32 x = 0.0f;
  f32 y = 0.0f;
  f32 z = 0.0f;
  f32 w = 0.0f;
};

constexpr Vec4 operator+(Vec4 a, Vec4 b) {
  return Vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
constexpr Vec4 operator-(Vec4 a, Vec4 b) {
  return Vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
constexpr Vec4 operator-(Vec4 a) { return Vec4{-a.x, -a.y, -a.z, -a.w}; }
constexpr Vec4 operator*(Vec4 a, f32 s) { return Vec4{a.x * s, a.y * s, a.z * s, a.w * s}; }
constexpr Vec4 operator*(f32 s, Vec4 a) { return a * s; }
constexpr Vec4 operator/(Vec4 a, f32 s) { return Vec4{a.x / s, a.y / s, a.z / s, a.w / s}; }

constexpr bool operator==(Vec4 a, Vec4 b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
constexpr bool operator!=(Vec4 a, Vec4 b) { return !(a == b); }

constexpr f32 Dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr f32 LengthSq(Vec4 a) { return Dot(a, a); }
inline    f32 Length(Vec4 a) { return Sqrt(LengthSq(a)); }
inline    Vec4 Normalized(Vec4 a) {
  const f32 len = Length(a);
  return len > 0.0f ? a / len : Vec4{};
}
inline bool NearlyEquals(Vec4 a, Vec4 b, f32 eps = kEpsilon) {
  return NearEq(a.x, b.x, eps) && NearEq(a.y, b.y, eps) && NearEq(a.z, b.z, eps) &&
         NearEq(a.w, b.w, eps);
}

// 便捷构造（Vec 是聚合体，这样写更省字）
constexpr Vec2 V2(f32 x, f32 y) { return Vec2{x, y}; }
constexpr Vec3 V3(f32 x, f32 y, f32 z) { return Vec3{x, y, z}; }
constexpr Vec4 V4(f32 x, f32 y, f32 z, f32 w) { return Vec4{x, y, z, w}; }

}  // namespace eng
