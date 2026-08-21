#pragma once
#include <array>
#include <cmath>
#include <functional>

template <typename T, size_t N>
using Vec = std::array<T, N>;
template <typename T, size_t W, size_t H>
using Mat = std::array<std::array<T, W>, H>;

template <typename T>
using Vec2 = Vec<T, 2>;
template <typename T>
using Vec3 = Vec<T, 3>;
template <typename T>
using Vec4 = Vec<T, 4>;

template <typename T>
using Mat2 = Mat<T, 2, 2>;
template <typename T>
using Mat3 = Mat<T, 3, 3>;
template <typename T>
using Mat4 = Mat<T, 4, 4>;

using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;

using Vec2i = Vec2<int>;
using Vec3i = Vec3<int>;
using Vec4i = Vec4<int>;

using Mat2f = Mat2<float>;
using Mat3f = Mat3<float>;
using Mat4f = Mat4<float>;

using Mat2i = Mat2<int>;
using Mat3i = Mat3<int>;
using Mat4i = Mat4<int>;

namespace std {
template <typename T, size_t H>
struct hash<Vec<T, H>> {
    size_t operator()(const Vec<T, H> &p) const {
      size_t hsh = 0;
      for (int i = 0; i < H; i++) {
        hsh += std::hash<T>()(p[i]);
      }
      return hsh;
    }
};
} // namespace std

template <typename T, size_t W, size_t H>
constexpr Mat<T, W, H> identity() {
  Mat<T, W, H> res;
  for (size_t i = 0; i < W; i++) {
    for (size_t j = 0; j < H; j++) {
      res[i][j] = i == j;
    }
  }
  return res;
}

// Source - https://stackoverflow.com/q/12289235
// Posted by Serg
// Retrieved 2026-08-12, License - CC BY-SA 3.0

template <typename T, size_t H>
T DotProduct(const Vec<T, H> &x, const Vec<T, H> &y) {
  T res = T(0);
  for (size_t i = 0; i < H; i++) {
    res += x[i] * y[i];
  }
  return res;
}

template <typename T>
Vec<T, 3> CrossProduct(const Vec<T, 3> &x, const Vec<T, 3> &y) {
  return Vec<T, 3> {
    x[1] * y[2] - x[2] * y[1], // i component
    x[2] * y[0] - x[0] * y[2], // j component
    x[0] * y[1] - x[1] * y[0]  // k component
  };
}

template <typename T, size_t H>
Vec<T, H> floor(const Vec<T, H> &v) {
  Vec<T, H> res;
  for (int i = 0; i < H; ++i) {
    res[i] = std::floor(v[i]);
  }
  return res;
}

template <typename T, size_t H>
Vec<T, H> fract(const Vec<T, H> v) {
  Vec<T, H> flr = floor(v);
  for (int i = 0; i < H; i++) {
    flr[i] = v[i] - flr[i];
  }
  return flr;
}

template <typename T, size_t H>
Vec<T, H> max(const Vec<T, H> &a, const Vec<T, H> &b) {
  Vec<T, H> res;
  for (size_t i = 0; i < H; ++i) {
    res[i] = std::max(a[i], b[i]);
  }
  return res;
}

template <typename T, size_t H>
Vec<T, H> abs(const Vec<T, H> &v) {
  Vec<T, H> res;
  for (int i = 0; i < H; ++i) {
    res[i] = std::abs(v[i]);
  }
  return res;
}

template <typename T, size_t H>
T Magnitude(const Vec<T, H> &v) {
  return std::sqrt(DotProduct(v, v));
}

template <typename T, size_t H>
T Magnitude2(const Vec<T, H> &v) {
  return DotProduct(v, v);
}

template <typename T, size_t H>
Vec<T, H> Normalize(const Vec<T, H> &v) {
  T len = std::sqrt(DotProduct(v, v));
  Vec<T, H> res;
  for (size_t i = 0; i < H; i++) {
    res[i] = v[i] / len;
  }
  return res;
}

template <typename T, size_t W, size_t H>
void MatrixVectorMult(
    const Mat<T, W, H> &mat, const Vec<T, H> &vec, Vec<T, H> &result) { // in matrix form:
                                                                        // result = mat *
                                                                        // vec;
  int i;
  for (i = 0; i < H; i++) {
    result[i] = DotProduct<T, H>(mat[i], vec);
  }
}

template <typename T, size_t W, size_t H>
constexpr Mat<T, W, H> Identity(Mat<T, W, H> &m) {
  for (size_t i = 0; i < W; i++) {
    for (size_t j = 0; j < H; j++) {
      m[i][j] = i == j;
    }
  }
  return m;
}
// Function to multiply a row by a column
template <typename T, size_t W, size_t H>
T _multiplyRowByColumn(
    const Mat<T, W, H> &mat1, const int row, const Mat<T, W, H> &mat2, const int col) {
  T sum = 0;
  for (int i = 0; i < mat2.size(); i++) {
    sum += mat1[row][i] * mat2[i][col];
  }
  return sum;
}

// Function to perform matrix multiplication
template <typename T, size_t W, size_t H>
void _matmul(
    const int start, const int end, const Mat<T, W, H> &A, const Mat<T, W, H> &B,
    Mat<T, W, H> &C) {
  for (int i = start; i < end; i++) {
    for (int j = 0; j < B[0].size(); j++) {
      C[i][j] = _multiplyRowByColumn(A, i, B, j);
    }
  }
}

template <typename T, size_t W, size_t H>
inline Mat<T, W, H> MatMul(Mat<T, W, H> &A, Mat<T, W, H> &B) {
  Mat<T, W, H> C;

  int start = 0;
  int end   = H;
  _matmul<T, W, H>(start, end, A, B, C);

  return C;
}

template <typename T, size_t H>
inline Vec<T, H> Negative(Vec<T, H> vec) {
  for (int i = 0; i < H; i++)
    vec[i] = -vec[i];
  return vec;
}

inline Mat4f BuildViewMatrix(const Vec3f cp, const Vec3f ca) {
  const float cosx = std::cos(ca[0]);
  const float cosy = std::cos(ca[1]);
  const float cosz = std::cos(ca[2]);
  const float sinx = std::sin(ca[0]);
  const float siny = std::sin(ca[1]);
  const float sinz = std::sin(ca[2]);

  Mat3f x = {
    { { 1, 0, 0 }, { 0, cosx, sinx }, { 0, -sinx, cosx } }
  };
  Mat3f y = {
    { { cosy, 0, -siny }, { 0, 1, 0 }, { siny, 0, cosy } }
  };
  Mat3f z = {
    { { cosz, sinz, 0 }, { -sinz, cosz, 0 }, { 0, 0, 1 } }
  };

  Mat3f a = MatMul(x, y);
  Mat3f R = MatMul(a, z);

  float tx = R[0][0] * (-cp[0]) + R[0][1] * (-cp[1]) + R[0][2] * (-cp[2]);
  float ty = R[1][0] * (-cp[0]) + R[1][1] * (-cp[1]) + R[1][2] * (-cp[2]);
  float tz = R[2][0] * (-cp[0]) + R[2][1] * (-cp[1]) + R[2][2] * (-cp[2]);

  return {
    { { R[0][0], R[0][1], R[0][2], tx },
     { R[1][0], R[1][1], R[1][2], ty },
     { R[2][0], R[2][1], R[2][2], tz },
     { 0, 0, 0, 1 } }
  };
}

inline Mat4f BuildProjectionMatrix(float fovRad, float aspect, float near, float far) {
  float tanHalf = tan(fovRad / 2.0f);
  float f_n     = far - near;

  return {
    { { 1.0f / (aspect * tanHalf), 0, 0, 0 },
     { 0, 1.0f / tanHalf, 0, 0 },
     { 0, 0, -(far + near) / f_n, -(2 * far * near) / f_n },
     { 0, 0, -1, 0 } }
  };
}

inline Vec4f ProjectPoint(Vec3f &p, Mat4f &transformMatrix) {
  Vec4f R;
  Vec4f p4 = { p[0], p[1], p[2], 1 };
  MatrixVectorMult(transformMatrix, p4, R);

  // Perspective divide
  return {
    R[0] / R[3], // NDC x: -1 to +1
    R[1] / R[3], // NDC y: -1 to +1
    R[2] / R[3], // NDC z: -1 to +1 (depth)
    R[3]         // Keep W for clipping tests
  };
}

inline Vec2i ToScreenCoords(Vec4f &ndc, int screenWidth, int screenHeight) {
  return {
    (int)((ndc[0] + 1.0f) * 0.5f * screenWidth),
    (int)((1.0f - ndc[1]) * 0.5f * screenHeight) // flip Y for screen coords
  };
}

// Scalar multiplication: vec * scalar
template <typename T, size_t N>
Vec<T, N> operator*(const Vec<T, N> &v, T scalar) {
  Vec<T, N> result;
  for (size_t i = 0; i < N; ++i)
    result[i] = v[i] * scalar;
  return result;
}

// Scalar multiplication: scalar * vec
template <typename T, size_t N>
Vec<T, N> operator*(T scalar, const Vec<T, N> &v) {
  return v * scalar;
}

// Element-wise multiplication: vec * vec
template <typename T, size_t N>
Vec<T, N> operator*(const Vec<T, N> &a, const Vec<T, N> &b) {
  Vec<T, N> result;
  for (size_t i = 0; i < N; ++i)
    result[i] = a[i] * b[i];
  return result;
}

// Scalar division: vec / scalar
template <typename T, size_t N>
Vec<T, N> operator/(const Vec<T, N> &v, T scalar) {
  Vec<T, N> result;
  for (size_t i = 0; i < N; ++i)
    result[i] = v[i] / scalar;
  return result;
}

// Addition: vec + vec
template <typename T, size_t N>
const Vec<T, N> operator+(const Vec<T, N> &a, const Vec<T, N> &b) {
  Vec<T, N> result;
  for (size_t i = 0; i < N; ++i)
    result[i] = a[i] + b[i];
  return result;
}

// Subtraction: vec - vec
template <typename T, size_t N>
Vec<T, N> operator-(const Vec<T, N> &a, const Vec<T, N> &b) {
  Vec<T, N> result;
  for (size_t i = 0; i < N; ++i)
    result[i] = a[i] - b[i];
  return result;
}

// Compound assignment operators
template <typename T, size_t N>
Vec<T, N> &operator*=(Vec<T, N> &v, T scalar) {
  for (size_t i = 0; i < N; ++i)
    v[i] *= scalar;
  return v;
}

template <typename T, size_t N>
Vec<T, N> &operator/=(Vec<T, N> &v, T scalar) {
  for (size_t i = 0; i < N; ++i)
    v[i] /= scalar;
  return v;
}

template <typename T, size_t N>
Vec<T, N> &operator+=(Vec<T, N> &a, const Vec<T, N> &b) {
  for (size_t i = 0; i < N; ++i)
    a[i] += b[i];
  return a;
}

template <typename T, size_t N>
Vec<T, N> &operator-=(Vec<T, N> &a, const Vec<T, N> &b) {
  for (size_t i = 0; i < N; ++i)
    a[i] -= b[i];
  return a;
}

template <typename T, size_t W, size_t H>
Mat<T, W, H> operator*(const Mat<T, W, H> &A, const Mat<T, W, H> &B) {
  return MatMul(A, B);
}

template <typename T, size_t W, size_t H>
Vec<T, H> operator*(const Mat<T, W, H> &mat, const Vec<T, H> &vec) {
  Vec<T, H> result;
  MatrixVectorMult(mat, vec, result);
  return result;
}

// Scalar :
template <typename T, size_t W, size_t H>
Mat<T, W, H> operator*(const Mat<T, W, H> &mat, T scalar) {
  Mat<T, W, H> result;
  for (size_t i = 0; i < W; ++i) {
    for (size_t j = 0; j < H; ++j) {
      result[i][j] = mat[i][j] * scalar;
    }
  }
  return result;
}

template <typename T, size_t W, size_t H>
Mat<T, W, H> &operator*=(Mat<T, W, H> &A, const Mat<T, W, H> &B) {
  A = MatMul(A, B);
  return A;
}

template <typename T, size_t W, size_t H>
Vec<T, H> &operator*=(Vec<T, H> &vec, const Mat<T, W, H> &mat) {
  Vec<T, H> result;
  MatrixVectorMult(mat, vec, result);
  vec = result;
  return vec;
}

template <typename T, size_t W, size_t H>
Mat<T, W, H> &operator*=(Mat<T, W, H> &mat, T scalar) {
  for (size_t i = 0; i < W; ++i) {
    for (size_t j = 0; j < H; ++j) {
      mat[i][j] *= scalar;
    }
  }
  return mat;
}
