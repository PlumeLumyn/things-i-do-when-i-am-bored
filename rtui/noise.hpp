#pragma once
#include "vec.hpp"

// Some useful functions
inline Vec3f mod289(Vec3f x) {
  return x - floor(x * (1.0f / 289.0f)) * 289.0f;
}
inline Vec2f mod289(Vec2f x) {
  return x - floor(x * (1.0f / 289.0f)) * 289.0f;
}
inline Vec3f permute(Vec3f x) {
  return mod289(((x * 34.0f) + Vec3f { 1.0f, 1.0f, 1.0f }) * x);
}

//
// Description : GLSL 2D simplex noise function
//      Author : Ian McEwan, Ashima Arts
//  Maintainer : ijm
//     Lastmod : 20110822 (ijm)
//     License :
//  Copyright (C) 2011 Ashima Arts. All rights reserved.
//  Distributed under the MIT License. See LICENSE file.
//  https://github.com/ashima/webgl-noise
//
inline float snoise(Vec2f v) {

  // Precompute values for skewed triangular grid
  const Vec4f C = Vec4f {
    0.211324865405187,
    // (3.0-sqrt(3.0))/6.0
    0.366025403784439,
    // 0.5*(sqrt(3.0)-1.0)
    -0.577350269189626,
    // -1.0 + 2.0 * C.x
    0.024390243902439
  };
  // 1.0 / 41.0

  // First corner (x0)
  float s  = DotProduct(v, Vec2f { 1.0f, 1.0f }) * C[1];
  Vec2f i  = floor(v + Vec2f { s, s });
  float t  = DotProduct(i, Vec2f { 1.0f, 1.0f }) * C[0];
  Vec2f x0 = v - i + Vec2f { t, t };

  // Other two corners (x1, x2)
  Vec2f i1 = Vec2f { 0.0f, 0.0f };
  i1       = (x0[0] > x0[1]) ? Vec2f { 1.0, 0.0 } : Vec2f { 0.0, 1.0 };
  Vec2f x1 = Vec2f { x0[0], x0[1] } + Vec2f { C[0], C[0] } - i1;
  Vec2f x2 = Vec2f { x0[0], x0[1] } + Vec2f { C[2], C[2] };

  // Do some permutations to avoid
  // truncation effects in permutation
  i       = mod289(i);
  Vec3f p = permute(
      permute(Vec3f { i[1], i[1], i[1] } + Vec3f { 0.0, i1[1], 1.0 }) +
      Vec3f { i[0], i[0], i[0] } + Vec3f { 0.0, i1[0], 1.0 });

  Vec3f m = max(
      Vec3f { 0.5, 0.5, 0.5 } -
          Vec3f { DotProduct(x0, x0), DotProduct(x1, x1), DotProduct(x2, x2) },
      Vec3f { 0.0, 0.0, 0.0 });

  m = m * m;
  m = m * m;

  // Gradients:
  //  41 pts uniformly over a line, mapped onto a diamond
  //  The ring size 17*17 = 289 is close to a multiple
  //      of 41 (41*7 = 287)

  Vec3f x  = 2.0f * fract(p * Vec3f { C[3], C[3], C[3] }) - Vec3f { 1.0f, 1.0f, 1.0f };
  Vec3f h  = abs(x) - Vec3f { 0.5, 0.5, 0.5 };
  Vec3f ox = floor(x + Vec3f { 0.5, 0.5, 0.5 });
  Vec3f a0 = x - ox;

  // Normalise gradients implicitly by scaling m
  // Approximation of: m *= inversesqrt(a0*a0 + h*h);
  m *= (1.79284291400159f - 0.85373472095314f * (DotProduct(a0, a0) + DotProduct(h, h)));

  // Compute final noise value at P
  Vec3f g    = Vec3f { 0.0, 0.0, 0.0 };
  g[0]       = a0[0] * x0[0] + h[0] * x0[1];
  Vec2f temp = Vec2f { a0[1], a0[2] } * Vec2f { x1[0], x2[0] } + Vec2f {
    h[1], h[2]
  } * Vec2f { x1[1], x2[1] };
  g[1] = temp[0];
  g[2] = temp[1];
  return 130.0 * DotProduct(m, g);
}
