// engine/base/mat.cpp
//
// 这里放"大且不热"的矩阵运算：投影、视图、求逆。
// 热路径的乘法与变换点留在 mat.h 里内联（每帧每个节点都要用）。
#include "base/mat.h"

namespace eng {

// ===========================================================================
// Mat3
// ===========================================================================
Mat3 Transposed(const Mat3& t) {
  Mat3 r;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      r.m[i][j] = t.m[j][i];
    }
  }
  return r;
}

bool Invert(const Mat3& in, Mat3* out) {
  if (out == nullptr) {
    return false;
  }
  const f32 a = in.m[0][0], b = in.m[0][1], c = in.m[0][2];
  const f32 d = in.m[1][0], e = in.m[1][1], f = in.m[1][2];
  const f32 g = in.m[2][0], h = in.m[2][1], i = in.m[2][2];

  const f32 A = (e * i - f * h);
  const f32 B = -(d * i - f * g);
  const f32 C = (d * h - e * g);
  const f32 det = a * A + b * B + c * C;
  if (det == 0.0f) {
    return false;
  }
  const f32 id = 1.0f / det;

  out->m[0][0] = A * id;
  out->m[0][1] = -(b * i - c * h) * id;
  out->m[0][2] = (b * f - c * e) * id;
  out->m[1][0] = B * id;
  out->m[1][1] = (a * i - c * g) * id;
  out->m[1][2] = -(a * f - c * d) * id;
  out->m[2][0] = C * id;
  out->m[2][1] = -(a * h - b * g) * id;
  out->m[2][2] = (a * e - b * d) * id;
  return true;
}

// ===========================================================================
// Mat4 —— 几何构造函数
// ===========================================================================
Mat4 Mat4::RotationAxis(Vec3 axis, f32 rad) {
  const Vec3 a = Normalized(axis);
  const f32 c = Cos(rad);
  const f32 s = Sin(rad);
  const f32 t = 1.0f - c;

  Mat4 r = Identity();
  r.m[0][0] = t * a.x * a.x + c;
  r.m[0][1] = t * a.x * a.y - s * a.z;
  r.m[0][2] = t * a.x * a.z + s * a.y;
  r.m[1][0] = t * a.x * a.y + s * a.z;
  r.m[1][1] = t * a.y * a.y + c;
  r.m[1][2] = t * a.y * a.z - s * a.x;
  r.m[2][0] = t * a.x * a.z - s * a.y;
  r.m[2][1] = t * a.y * a.z + s * a.x;
  r.m[2][2] = t * a.z * a.z + c;
  return r;
}

// 标准 OpenGL 透视：NDC 的 z ∈ [-1, 1]
// ★ 这个公式是照着教科书按行抄的 —— 见 mat.h 顶部"为什么选行主序"第 2 条 ★
//   自检会验端点：z=-znear → z_ndc=-1；z=-zfar → z_ndc=+1。
Mat4 Mat4::Perspective(f32 fovy_rad, f32 aspect, f32 znear, f32 zfar) {
  const f32 f = 1.0f / Tan(fovy_rad * 0.5f);
  Mat4 r;
  r.m[0][0] = f / aspect;
  r.m[1][1] = f;
  r.m[2][2] = (zfar + znear) / (znear - zfar);
  r.m[2][3] = (2.0f * zfar * znear) / (znear - zfar);
  r.m[3][2] = -1.0f;
  r.m[3][3] = 0.0f;
  return r;
}

Mat4 Mat4::Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 znear, f32 zfar) {
  Mat4 r = Identity();
  r.m[0][0] = 2.0f / (right - left);
  r.m[1][1] = 2.0f / (top - bottom);
  r.m[2][2] = -2.0f / (zfar - znear);
  r.m[0][3] = -(right + left) / (right - left);
  r.m[1][3] = -(top + bottom) / (top - bottom);
  r.m[2][3] = -(zfar + znear) / (zfar - znear);
  return r;
}

// 标准 gluLookAt（右手系，相机看向 -Z）
// ★ 自检会验：target 变换后应落在 (0, 0, -|target-eye|) ★
Mat4 Mat4::LookAt(Vec3 eye, Vec3 target, Vec3 up) {
  const Vec3 f = Normalized(target - eye);
  const Vec3 s = Normalized(Cross(f, up));
  const Vec3 u = Cross(s, f);

  Mat4 r = Identity();
  r.m[0][0] = s.x;
  r.m[0][1] = s.y;
  r.m[0][2] = s.z;
  r.m[0][3] = -Dot(s, eye);
  r.m[1][0] = u.x;
  r.m[1][1] = u.y;
  r.m[1][2] = u.z;
  r.m[1][3] = -Dot(u, eye);
  r.m[2][0] = -f.x;
  r.m[2][1] = -f.y;
  r.m[2][2] = -f.z;
  r.m[2][3] = Dot(f, eye);
  return r;
}

Mat4 Transposed(const Mat4& m) {
  Mat4 r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r.m[i][j] = m.m[j][i];
    }
  }
  return r;
}

namespace {

// 3x3 余子式：删掉第 r 行第 c 列后的行列式。
// a 是行主序展平数组（a[row*4+col]）。
f32 Minor3(const f32* a, int r, int c) {
  int ri[3];
  int ci[3];
  int nr = 0;
  int nc = 0;
  for (int i = 0; i < 4; ++i) {
    if (i != r) {
      ri[nr++] = i;
    }
  }
  for (int j = 0; j < 4; ++j) {
    if (j != c) {
      ci[nc++] = j;
    }
  }
  const f32 a00 = a[ri[0] * 4 + ci[0]], a01 = a[ri[0] * 4 + ci[1]], a02 = a[ri[0] * 4 + ci[2]];
  const f32 a10 = a[ri[1] * 4 + ci[0]], a11 = a[ri[1] * 4 + ci[1]], a12 = a[ri[1] * 4 + ci[2]];
  const f32 a20 = a[ri[2] * 4 + ci[0]], a21 = a[ri[2] * 4 + ci[1]], a22 = a[ri[2] * 4 + ci[2]];
  return a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
         a02 * (a10 * a21 - a11 * a20);
}

}  // namespace

// 4x4 求逆：★ 按伴随矩阵的定义写，索引显式 ★
//
// ★ 为什么不用那段常见的 unrolled 代码（MESA 的 gluInvertMatrix）★
//   那段代码假设数组是 ★OpenGL 列主序★ 的。我们的约定是行主序，
//   照搬过来算出来是 (A⁻¹)ᵀ —— ★ 而且不会崩、不会报错，只是结果悄悄不对 ★。
//   实测就是这样翻车的（自检里 M * Inv(M) = I 一次红了 15 条）。
//   所以这里换成定义式——慢一点，但索引没有歧义；而且 Invert 不是每帧热路径。
bool Invert(const Mat4& in, Mat4* out) {
  if (out == nullptr) {
    return false;
  }
  const f32* a = &in.m[0][0];

  f32 cof[16];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      const f32 sign = ((r + c) & 1) ? -1.0f : 1.0f;
      cof[r * 4 + c] = sign * Minor3(a, r, c);
    }
  }

  // det：沿第 0 行展开
  const f32 det = a[0] * cof[0] + a[1] * cof[1] + a[2] * cof[2] + a[3] * cof[3];
  if (det == 0.0f) {
    return false;
  }
  const f32 id = 1.0f / det;

  // ★ adj(A)[i][j] = cof[j][i] ★ —— 伴随矩阵是「余子式矩阵的转置」，这一步别写反
  f32* o = &out->m[0][0];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      o[i * 4 + j] = cof[j * 4 + i] * id;
    }
  }
  return true;
}

}  // namespace eng
