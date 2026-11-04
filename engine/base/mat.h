// engine/base/mat.h
//
// 矩阵约定：
//   存储：行主序 m[row][col]
//   变换：列向量 out = M * v，平移在最后一列（m[0][3]=tx …）
//   照抄 OpenGL 教科书公式不会错位。
#pragma once

#include "base/math.h"
#include "base/types.h"
#include "base/vec.h"

namespace eng {

// ===========================================================================
// Mat3 —— 2D 仿射（平移 / 旋转 / 缩放）
// ===========================================================================
struct Mat3 {
  f32 m[3][3] = {};  // [row][col]

  static Mat3 Identity() {
    Mat3 r;
    r.m[0][0] = 1.0f;
    r.m[1][1] = 1.0f;
    r.m[2][2] = 1.0f;
    return r;
  }

  static Mat3 Translation(Vec2 t) {
    Mat3 r = Identity();
    r.m[0][2] = t.x;  // ★ 平移在最后一列 ★
    r.m[1][2] = t.y;
    return r;
  }

  static Mat3 Scale(Vec2 s) {
    Mat3 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = 1.0f;
    return r;
  }

  static Mat3 Rotation(f32 rad) {
    const f32 c = Cos(rad);
    const f32 s = Sin(rad);
    Mat3 r;
    r.m[0][0] = c;
    r.m[0][1] = -s;
    r.m[1][0] = s;
    r.m[1][1] = c;
    r.m[2][2] = 1.0f;
    return r;
  }
};

// (A*B)[i][j] = sum_k A[i][k]*B[k][j] —— 行主序下就是书上那个公式
inline Mat3 operator*(const Mat3& a, const Mat3& b) {
  Mat3 r;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    }
  }
  return r;
}

// 变换一个点（隐含 w=1，要加平移）
inline Vec2 TransformPoint(const Mat3& t, Vec2 p) {
  return Vec2{t.m[0][0] * p.x + t.m[0][1] * p.y + t.m[0][2],
              t.m[1][0] * p.x + t.m[1][1] * p.y + t.m[1][2]};
}

// 变换一个方向（w=0，不加平移）—— 法线/速度要用这个
inline Vec2 TransformDir(const Mat3& t, Vec2 d) {
  return Vec2{t.m[0][0] * d.x + t.m[0][1] * d.y, t.m[1][0] * d.x + t.m[1][1] * d.y};
}

Mat3 Transposed(const Mat3& t);
bool Invert(const Mat3& t, Mat3* out);  // 奇异返回 false

// ===========================================================================
// Mat4 —— 3D（★ 3D 接缝：现在就写全，不能等 M7 ★）
// ===========================================================================
struct Mat4 {
  f32 m[4][4] = {};  // [row][col]

  static Mat4 Identity() {
    Mat4 r;
    r.m[0][0] = 1.0f;
    r.m[1][1] = 1.0f;
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f;
    return r;
  }

  static Mat4 Translation(Vec3 t) {
    Mat4 r = Identity();
    r.m[0][3] = t.x;  // ★ 平移在最后一列 ★
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
  }

  static Mat4 Scale(Vec3 s) {
    Mat4 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    r.m[3][3] = 1.0f;
    return r;
  }

  static Mat4 RotationX(f32 rad) {
    const f32 c = Cos(rad);
    const f32 s = Sin(rad);
    Mat4 r = Identity();
    r.m[1][1] = c;
    r.m[1][2] = -s;
    r.m[2][1] = s;
    r.m[2][2] = c;
    return r;
  }

  static Mat4 RotationY(f32 rad) {
    const f32 c = Cos(rad);
    const f32 s = Sin(rad);
    Mat4 r = Identity();
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
  }

  static Mat4 RotationZ(f32 rad) {
    const f32 c = Cos(rad);
    const f32 s = Sin(rad);
    Mat4 r = Identity();
    r.m[0][0] = c;
    r.m[0][1] = -s;
    r.m[1][0] = s;
    r.m[1][1] = c;
    return r;
  }

  static Mat4 RotationAxis(Vec3 axis, f32 rad);  // 实现在 mat.cpp

  // 标准 OpenGL 透视（NDC 的 z ∈ [-1, 1]）
  static Mat4 Perspective(f32 fovy_rad, f32 aspect, f32 znear, f32 zfar);

  static Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 znear, f32 zfar);

  static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                  a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    }
  }
  return r;
}

inline Vec4 operator*(const Mat4& m, const Vec4& v) {
  return Vec4{m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w,
              m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w,
              m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w,
              m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w};
}

// 点：w=1，★ 做透视除法 ★
inline Vec3 TransformPoint(const Mat4& m, Vec3 p) {
  const Vec4 r = m * Vec4{p.x, p.y, p.z, 1.0f};
  if (r.w != 0.0f && r.w != 1.0f) {
    const f32 inv = 1.0f / r.w;
    return Vec3{r.x * inv, r.y * inv, r.z * inv};
  }
  return Vec3{r.x, r.y, r.z};
}

// 方向：w=0，不做透视除法
inline Vec3 TransformDir(const Mat4& m, Vec3 d) {
  return Vec3{m.m[0][0] * d.x + m.m[0][1] * d.y + m.m[0][2] * d.z,
              m.m[1][0] * d.x + m.m[1][1] * d.y + m.m[1][2] * d.z,
              m.m[2][0] * d.x + m.m[2][1] * d.y + m.m[2][2] * d.z};
}

Mat4 Transposed(const Mat4& m);
bool Invert(const Mat4& m, Mat4* out);  // 奇异返回 false

}  // namespace eng
