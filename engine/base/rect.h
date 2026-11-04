// engine/base/rect.h
//
// 命名沿用 Godot 的 Rect2（position + size），见 docs/00 D-14。
// ★ 坐标系 ★：Y 向下（屏幕坐标系），所以在屏幕上 Top < Bottom。
//   2D 用这个；3D 最终会经投影矩阵转到屏幕空间，同样落到这里。
#pragma once

#include "base/math.h"
#include "base/types.h"
#include "base/vec.h"

namespace eng {

struct Rect2 {
  Vec2 position;  // 左上角
  Vec2 size;

  constexpr f32 Left() const { return position.x; }
  constexpr f32 Top() const { return position.y; }
  constexpr f32 Right() const { return position.x + size.x; }
  constexpr f32 Bottom() const { return position.y + size.y; }

  constexpr Vec2 End() const { return Vec2{Right(), Bottom()}; }
  constexpr Vec2 Center() const {
    return Vec2{position.x + size.x * 0.5f, position.y + size.y * 0.5f};
  }
  constexpr f32 Area() const { return size.x * size.y; }

  constexpr bool HasPoint(Vec2 p) const {
    return p.x >= position.x && p.x < Right() && p.y >= position.y && p.y < Bottom();
  }

  // 相交：★ 边界相接不算相交 ★（与 Godot 的 intersects 一致，
  // 这样"地图格紧挨着"不会被当成碰撞）
  constexpr bool Intersects(const Rect2& o) const {
    return o.Left() < Right() && Left() < o.Right() && o.Top() < Bottom() &&
           Top() < o.Bottom();
  }

  constexpr Rect2 Intersection(const Rect2& o) const {
    const f32 l = Max(Left(), o.Left());
    const f32 t = Max(Top(), o.Top());
    const f32 rr = Min(Right(), o.Right());
    const f32 bb = Min(Bottom(), o.Bottom());
    if (rr <= l || bb <= t) {
      return Rect2{Vec2{l, t}, Vec2{}};  // 空矩形
    }
    return Rect2{Vec2{l, t}, Vec2{rr - l, bb - t}};
  }

  // 合并（包含两者的最小矩形）。空矩形参与合并时返回另一个。
  constexpr Rect2 Merged(const Rect2& o) const {
    if (size.x <= 0.0f || size.y <= 0.0f) {
      return o;
    }
    if (o.size.x <= 0.0f || o.size.y <= 0.0f) {
      return *this;
    }
    const f32 l = Min(Left(), o.Left());
    const f32 t = Min(Top(), o.Top());
    const f32 rr = Max(Right(), o.Right());
    const f32 bb = Max(Bottom(), o.Bottom());
    return Rect2{Vec2{l, t}, Vec2{rr - l, bb - t}};
  }
};

constexpr bool operator==(const Rect2& a, const Rect2& b) {
  return a.position == b.position && a.size == b.size;
}
constexpr bool operator!=(const Rect2& a, const Rect2& b) { return !(a == b); }

// 由 min/max 角构造
constexpr Rect2 RectFromMinMax(Vec2 lo, Vec2 hi) {
  return Rect2{lo, Vec2{hi.x - lo.x, hi.y - lo.y}};
}

}  // namespace eng
