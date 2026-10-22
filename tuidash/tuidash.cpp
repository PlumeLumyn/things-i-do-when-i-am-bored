#define STB_IMAGE_IMPLEMENTATION
#define INPUTS_HPP_IMPLEMENTATION
#include "../external/stb_image.h"
#include "../rtui/inputs.hpp"
#include "../rtui/noise.hpp"
#include "../rtui/path.hpp"
#include "../rtui/screen.hpp"
#include "../rtui/vec.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>

struct Vertex {
    Vec3f pos;
    Vec3f normal;
    Vec2f uv;
};

struct Image {
    int width;
    int height;
    int channels;
    unsigned char *data;
};

const std::vector<std::vector<int>> level {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};

Image loadTexture(const std::string &path) {
  Image tex;
  int texWidth, texHeight, texChannels;
  unsigned char *data = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, 4);
  if (!data) {
    std::cerr << "Failed to load texture: " << path << std::endl;
    return { 0, 0, 0, nullptr };
  }
  tex.width    = texWidth;
  tex.height   = texHeight;
  tex.channels = texChannels;
  tex.data     = data;
  return tex;
}

inline int floorDiv(int value, int divisor) {
  return value >= 0 ? value / divisor : (value - divisor + 1) / divisor;
}

void drawLine(Screen::Window &w, Vec4f o, Vec4f d, Vec3f col = { 1.0, 1.0, 1.0 }) {
  Vec2i oi = ToScreenCoords(o, w.getWidth(), w.getSubPixelHeight());
  Vec2i di = ToScreenCoords(d, w.getWidth(), w.getSubPixelHeight());
  int x0 = oi[0], y0 = oi[1];
  int x1 = di[0], y1 = di[1];

  int dx  = abs(x1 - x0);
  int dy  = -abs(y1 - y0);
  int sx  = x0 < x1 ? 1 : -1;
  int sy  = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  int steps = std::max(abs(x1 - x0), abs(y1 - y0));
  double z  = o[2] * o[3];
  double dz = steps > 0 ? (d[2] * d[3] - o[2] * o[3]) / steps : 0;

  while (true) {
    w.putPixel(x0, y0, z, col);

    if (x0 == x1 && y0 == y1) break;

    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
    z += dz;
  }
}

using Shader = std::function<bool(Vertex, Vec3f &)>;
bool isInsidePlane(const Vec4f &clipPos, int plane) {
  switch (plane) {
  case 0:
    return clipPos[0] >= -clipPos[3]; // Left
  case 1:
    return clipPos[0] <= clipPos[3]; // Right
  case 2:
    return clipPos[1] >= -clipPos[3]; // Bottom
  case 3:
    return clipPos[1] <= clipPos[3]; // Top
  case 4:
    return clipPos[2] >= -clipPos[3]; // Near
  case 5:
    return clipPos[2] <= clipPos[3]; // Far
  default:
    return true;
  }
}

// Interpolate between two vertices in clip space
float computePlaneIntersection(const Vec4f &p1, const Vec4f &p2, int plane) {
  float d1, d2;
  switch (plane) {
  case 0:
    d1 = p1[3] + p1[0];
    d2 = p2[3] + p2[0];
    break; // Left
  case 1:
    d1 = p1[3] - p1[0];
    d2 = p2[3] - p2[0];
    break; // Right
  case 2:
    d1 = p1[3] + p1[1];
    d2 = p2[3] + p2[1];
    break; // Bottom
  case 3:
    d1 = p1[3] - p1[1];
    d2 = p2[3] - p2[1];
    break; // Top
  case 4:
    d1 = p1[3] + p1[2];
    d2 = p2[3] + p2[2];
    break; // Near
  case 5:
    d1 = p1[3] - p1[2];
    d2 = p2[3] - p2[2];
    break; // Far
  default:
    return 0.0f;
  }
  if (std::abs(d1 - d2) < 1e-6f) return 0.0f;
  return d1 / (d1 - d2);
}

Vertex lerpVertex(const Vertex &v1, const Vertex &v2, float t) {
  Vertex result;
  result.pos    = v1.pos * (1.0f - t) + v2.pos * t;
  result.uv     = v1.uv * (1.0f - t) + v2.uv * t;
  result.normal = v1.normal * (1.0f - t) + v2.normal * t;
  return result;
}

// Sutherland-Hodgman clipping against one plane
void clipAgainstPlane(
    std::vector<Vertex> &vertices, std::vector<Vec4f> &clipPositions, int plane) {

  std::vector<Vertex> outVerts;
  std::vector<Vec4f> outClip;

  for (size_t i = 0; i < vertices.size(); i++) {
    size_t j = (i + 1) % vertices.size();

    bool in1 = isInsidePlane(clipPositions[i], plane);
    bool in2 = isInsidePlane(clipPositions[j], plane);

    if (in1 && in2) {
      // Both inside
      outVerts.push_back(vertices[j]);
      outClip.push_back(clipPositions[j]);
    } else if (in1 && !in2) {
      // Exiting: add intersection
      float t = computePlaneIntersection(clipPositions[i], clipPositions[j], plane);
      outVerts.push_back(lerpVertex(vertices[i], vertices[j], t));
      outClip.push_back(clipPositions[i] * (1.0f - t) + clipPositions[j] * t);
    } else if (!in1 && in2) {
      // Entering: add intersection + vertex j
      float t = computePlaneIntersection(clipPositions[i], clipPositions[j], plane);
      outVerts.push_back(lerpVertex(vertices[i], vertices[j], t));
      outClip.push_back(clipPositions[i] * (1.0f - t) + clipPositions[j] * t);
      outVerts.push_back(vertices[j]);
      outClip.push_back(clipPositions[j]);
    }
  }

  vertices      = outVerts;
  clipPositions = outClip;
}

void drawTriangleRasterize(
    Screen::Window &window, std::array<Vertex, 3> &tri, std::array<Vec4f, 3> &tClip,
    Shader shader = nullptr, Vec2f offset = { 0.0f, 0.0f }) {
  // Perspective divide to NDC
  std::array<Vec4f, 3> t;
  for (int v = 0; v < 3; v++) {
    Vec3f ndc = ToNDC(tClip[v]);
    t[v]      = Vec4f { ndc[0], ndc[1], ndc[2], tClip[v][3] };
  }

  Vec2i iOffset = {
    static_cast<int>(offset[0] * window.getSubPixelHeight()),
    static_cast<int>(offset[1] * -window.getSubPixelHeight())
  };

  Vec2i p0 =
      ToScreenCoords(t[0], window.getWidth(), window.getSubPixelHeight()) + iOffset;
  Vec2i p1 =
      ToScreenCoords(t[1], window.getWidth(), window.getSubPixelHeight()) + iOffset;
  Vec2i p2 =
      ToScreenCoords(t[2], window.getWidth(), window.getSubPixelHeight()) + iOffset;

  // Bounding box
  int minX = std::max(-1, std::min({ p0[0], p1[0], p2[0] }));
  int maxX = std::min(window.getWidth(), std::max({ p0[0], p1[0], p2[0] }));
  int minY = std::max(-1, std::min({ p0[1], p1[1], p2[1] }));
  int maxY = std::min(window.getSubPixelHeight() - 1, std::max({ p0[1], p1[1], p2[1] }));

  // Precompute for barycentric
  float denom = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1]);
  if (std::abs(denom) < 1e-6f) return; // Degenerate triangle

  float invDenom = 1.0f / denom;

  Vec3f invW, z;
  std::array<Vec2f, 3> uv_w;
  std::array<Vec3f, 3> normal_w;
  Vertex v;

  for (size_t i = 0; i < 3; i++) {
    // Perspective-correct interpolation: store 1/w
    invW[i] = 1.0f / t[i][3];

    // Depth values (z/w for depth buffer)
    z[i] = t[i][2] * invW[i];

    // Datas divided by w for perspective-correct interpolation
    uv_w[i]     = tri[i].uv * invW[i];
    normal_w[i] = tri[i].normal * invW[i];
  }

  auto edgeBias = [](const Vec2i &from, const Vec2i &to) -> float {
    // Top edge: horizontal edge going left (to.x < from.x)
    // Left edge: edge going down (to.y > from.y)
    bool isTop  = (from[1] == to[1]) && (to[0] < from[0]);
    bool isLeft = (to[1] > from[1]);
    // Top-left edges INCLUDE zero (draw on edge), others EXCLUDE zero (skip edge)
    return (isTop || isLeft) ? 0.0f : -1e-3f;
  };

  float bias0 = edgeBias(p1, p2);
  float bias1 = edgeBias(p2, p0);
  float bias2 = edgeBias(p0, p1);

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      Vec3f w;
      w[0] = ((p1[1] - p2[1]) * (x - p2[0]) + (p2[0] - p1[0]) * (y - p2[1])) * invDenom;
      w[1] = ((p2[1] - p0[1]) * (x - p2[0]) + (p0[0] - p2[0]) * (y - p2[1])) * invDenom;
      w[2] = 1.0f - w[0] - w[1];

      // Reject if outside OR on a non-owned edge
      if (w[0] < bias0 || w[1] < bias1 || w[2] < bias2) continue;

      // Interpolate depth
      float localZ    = w[0] * z[0] + w[1] * z[1] + w[2] * z[2];
      float localInvW = w[0] * invW[0] + w[1] * invW[1] + w[2] * invW[2];

      // // Perspective-correct UV interpolation
      v.pos = { static_cast<float>(x), static_cast<float>(y), localZ };
      v.uv  = (w[0] * uv_w[0] + w[1] * uv_w[1] + w[2] * uv_w[2]) / localInvW;
      v.normal =
          (w[0] * normal_w[0] + w[1] * normal_w[1] + w[2] * normal_w[2]) / localInvW;

      if (shader) {
        Vec3f fragColor = { 1.0f, 1.0f, 1.0f };
        if (shader(v, fragColor)) window.putPixel(x, y, -localZ, fragColor);
      }
    }
  }
}

void drawTriangle(
    Screen::Window &window, std::array<Vertex, 3> &vertexData, Mat4f &transformMatrix,
    Shader shader = nullptr, Vec2f offset = { 0.0f, 0.0f }) {
  // Transform to clip space (NO perspective divide yet)
  std::array<Vec4f, 3> clipSpace;
  clipSpace[0] = ToClipSpace(vertexData[0].pos, transformMatrix);
  clipSpace[1] = ToClipSpace(vertexData[1].pos, transformMatrix);
  clipSpace[2] = ToClipSpace(vertexData[2].pos, transformMatrix);

  // Fast path: check if triangle is completely inside frustum
  bool needsClipping = false;
  for (int i = 0; i < 3; i++) {
    const Vec4f &c = clipSpace[i];
    if (c[0] < -c[3] || c[0] > c[3] || // Left/Right
        c[1] < -c[3] || c[1] > c[3] || // Bottom/Top
        c[2] < -c[3] || c[2] > c[3]) { // Near/Far
      needsClipping = true;
      break;
    }
  }

  // Fast path: no clipping needed
  if (!needsClipping) {
    drawTriangleRasterize(window, vertexData, clipSpace, shader, offset);
    return;
  }

  // Slow path: clip the triangle
  std::vector<Vertex> vertices     = { vertexData[0], vertexData[1], vertexData[2] };
  std::vector<Vec4f> clipPositions = { clipSpace[0], clipSpace[1], clipSpace[2] };

  // Clip against all 6 frustum planes
  for (int plane = 0; plane < 6; plane++) {
    clipAgainstPlane(vertices, clipPositions, plane);
    if (vertices.size() < 3) return; // Completely clipped
  }

  for (size_t i = 1; i + 1 < vertices.size(); i++) {
    std::array<Vertex, 3> tri  = { vertices[0], vertices[i], vertices[i + 1] };
    std::array<Vec4f, 3> tClip = {
      clipPositions[0], clipPositions[i], clipPositions[i + 1]
    };

    drawTriangleRasterize(window, tri, tClip, shader, offset);
  }
}

void drawBox(
    Mat4f &transformMatrix, const Image &texture, const std::array<Vertex, 36> &cube,
    Screen::ScreenController &sController, int windowId, const Vec3f &lightDir) {
  for (size_t k = 0; k < cube.size() / 3; k++) {
    std::array<Vertex, 3> vertexData;
    vertexData[0] = cube[k * 3];
    vertexData[1] = cube[k * 3 + 1];
    vertexData[2] = cube[k * 3 + 2];

    float light = DotProduct(Normalize(vertexData[0].normal), lightDir);

    drawTriangle(
        sController.getWindow(windowId),
        vertexData,
        transformMatrix,
        [&texture, &light](Vertex v, Vec3f &fragColor) {
          // Compute texture coordinates
          int texX = std::max(
              std::min(static_cast<int>(v.uv[0] * (texture.width)), texture.width - 1),
              0);
          int texY = std::max(
              std::min(static_cast<int>(v.uv[1] * (texture.height)), texture.height - 1),
              0);
          int texIndex = (texY * texture.width + texX) * 4;

          // Check alpha transparency
          if (texture.data[texIndex + 3] < 128) return false;

          // Compute fragment color based on texture and lighting
          fragColor = {
            texture.data[texIndex] / 255.0f,
            texture.data[texIndex + 1] / 255.0f,
            texture.data[texIndex + 2] / 255.0f
          };
          float lightPow = (v.pos[2]) * (4.0f + light);
          fragColor *= lightPow;
          fragColor += Vec3f { 0.6f, 0.6f, 0.9f } * std::max(0.0f, 0.5f - lightPow);

          // Clamp fragment colors
          if (fragColor[0] > 1.0f) fragColor[0] = 1.0f;
          if (fragColor[1] > 1.0f) fragColor[1] = 1.0f;
          if (fragColor[2] > 1.0f) fragColor[2] = 1.0f;
          if (fragColor[0] < 0.0f) fragColor[0] = 0.0f;
          if (fragColor[1] < 0.0f) fragColor[1] = 0.0f;
          if (fragColor[2] < 0.0f) fragColor[2] = 0.0f;

          return true;
        });
  }
}

int main(void) {
  try {
    std::string executablePath = getExecutablePath();
    std::cout << "Executable Path: " << executablePath << std::endl;

    std::string directory = getDirectoryFromPath(executablePath);
    std::cout << "Directory: " << directory << std::endl;

    setCurrentWorkingDirectory(directory);
    std::cout << "Current working directory set to: " << directory << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  const std::array textures = {
    loadTexture("./resources/textures/block.png"),
    loadTexture("./resources/textures/player.png"),
  };

  Inputs::InputController iController;
  Screen::ScreenController sController;

  // All triangles of the cube (two per face so 12*3 = 36 vertices)
  // Format: x, y, z, nx, ny, nz, u, v
  // clang-format off
  const std::array<Vertex, 36> cube = {
      // Back face (normal: 0, 0, -1)
      -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f,
      -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
      -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f,
                                             
      // Front face (normal: 0, 0, 1) 
      -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f,
       0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f,
      -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,
      -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f,
                                             
      // Left face (normal: -1, 0, 0) 
      -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
      -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,
      -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f,
      -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,
      -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
                                             
      // Right face (normal: 1, 0, 0) 
       0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
       0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,
       0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f,
       0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f,
       0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,
       0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
                                             
      // Bottom face (normal: 0, -1, 0)
      -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f,
       0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
       0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f,
      -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f,
      -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f,
                                             
      // Top face (normal: 0, 1, 0)
      -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f,
      -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f,
      -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f,
  };
  // clang-format on

  Vec3f camera      = { 0.0f, 3.0f, 8.0f };
  Vec3f pos         = { 0.0f, 0.5f, 0.0f };
  Vec3f rot         = { 0.0f, 0.0f, 0.0f };
  Vec3f cameraAngle = { 0.0f, 0.0f, 0.0f };
  Vec3f vel         = { 0.0, 0.0, 0.0 };
  Vec3f gravity     = { 0.0, -0.04f, 0.0 };
  Mat4f viewMatrix;
  size_t id = 0;
  Identity(viewMatrix);
  Mat4f projectionMatrix;
  Identity(projectionMatrix);
  Mat4f modelMatrix;
  Identity(modelMatrix);

  // Variables pour le calcul du FPS
  float fps          = 0.0f;
  auto lastFpsUpdate = std::chrono::steady_clock::now();
  int frameCount     = 0;
  bool canJump       = true;
  bool isJumping     = false; // Track if we're in a jump
  int jumpFrames     = 0;     // Track how long jump has been held
  int lastY          = -1;

  size_t windowId = sController.createWindow(0, 0, 0, 0);
  while (1) {
    auto frameStart = std::chrono::steady_clock::now();

    float SPEED = 0.04f;
    if (iController.isJustPressed(Inputs::Keycode::MouseLeft)) {
    }
    if (iController.isJustPressed(Inputs::Keycode::MouseRight)) {
    }
    if (iController.isJustPressed(Inputs::Keycode::Space) && canJump && vel[1] > -0.2f) {
      // Initial jump impulse
      vel[1]  = 0.6f;
      canJump = false;
    }

    iController.refresh();

    vel += gravity;
    pos += vel;

    vel *= 0.9f;

    pos[0] += 0.2f;

    float closestFloor = 0.5f;
    int x              = (int)std::round(pos[0] + 0.5f);
    int y              = (int)pos[1];
    if (x >= 0 && y >= 0 && y < level.size() && x < level[0].size()) {
      if (level[level.size() - y - 1][x]) {
        if (lastY < y) {
          vel[1] = 0.0f;
          pos[1] -= (pos[1] - (int)pos[1]) + 10e-6;
        } else
          break;
      }
      for (int i = y; i > 0; i--) {
        if (level[level.size() - i][x]) {
          closestFloor = i + 0.5f;
          break;
        }
      }
    }
    lastY = y;
    if (pos[1] < closestFloor) {
      vel[1] = 0.0f;
      pos[1] = closestFloor;
      rot[2] -= std::fmod(rot[2], M_PI_2);
      if (rot[2] < -M_PI * 2) rot[2] = -std::fmod(-rot[2], M_PI * 2);
      canJump = true;
    } else {
      rot[2] -= 0.14f;
    }

    sController.getWindow(windowId).clear();
    const float NEAR_CLIP = 0.0001;
    const float FAR_CLIP  = 20.0;
    const float RATIO =
        static_cast<float>(sController.getWindow(windowId).getWidth()) /
        static_cast<float>(sController.getWindow(windowId).getSubPixelHeight());

    viewMatrix = BuildViewMatrix(Vec3f { pos[0], 0.0f, 0.0f } + camera, cameraAngle);
    float fovY = 80.0f * (M_PI / 180.0f); // 30 degrees in radians
    projectionMatrix = BuildProjectionMatrix(fovY, RATIO, NEAR_CLIP, FAR_CLIP);

    Mat4f vpMatrix = MatMul(projectionMatrix, viewMatrix);

    Mat3f viewMatrix3 = {
      viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0],
      viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1],
      viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]
    };
    Vec3f lightDir;
    MatrixVectorMult(viewMatrix3, { 0.0f, 0.0f, 1.0f }, lightDir);
    lightDir = Normalize(lightDir);

    Identity(modelMatrix);
    float cosX = std::cos(rot[0]);
    float sinX = std::sin(rot[0]);
    float cosY = std::cos(rot[1]);
    float sinY = std::sin(rot[1]);
    float cosZ = std::cos(rot[2]);
    float sinZ = std::sin(rot[2]);

    Mat4f rotX = {
      { { 1, 0, 0, 0 }, { 0, cosX, -sinX, 0 }, { 0, sinX, cosX, 0 }, { 0, 0, 0, 1 } }
    };

    Mat4f rotY = {
      { { cosY, 0, sinY, 0 }, { 0, 1, 0, 0 }, { -sinY, 0, cosY, 0 }, { 0, 0, 0, 1 } }
    };

    Mat4f rotZ = {
      { { cosZ, -sinZ, 0, 0 }, { sinZ, cosZ, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } }
    };

    Mat4f rotation = MatMul(MatMul(rotZ, rotY), rotX);

    modelMatrix           = MatMul(modelMatrix, rotation);
    modelMatrix[0][3]     = pos[0];
    modelMatrix[1][3]     = pos[1];
    modelMatrix[2][3]     = pos[2];
    Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
    drawBox(transformMatrix, textures[1], cube, sController, windowId, lightDir);

    Identity(modelMatrix);
    modelMatrix[0][3] = 0;
    modelMatrix[1][3] = -0.5f;
    modelMatrix[2][3] = 0;
    for (int i = -20; i <= 20; i++) {
      int x = i + (int)pos[0];
      for (int j = -1; j <= 1; j++) {
        modelMatrix[0][3] = x;
        modelMatrix[1][3] = -0.5f;
        modelMatrix[2][3] = j;
        transformMatrix   = MatMul(vpMatrix, modelMatrix);
        drawBox(transformMatrix, textures[0], cube, sController, windowId, lightDir);
      }
      for (int j = 0; j < 8; j++) {
        if (x > 0 && x < level[j].size() && level[j][x]) {
          modelMatrix[0][3] = x;
          modelMatrix[1][3] = (8 - j) - 0.5f;
          modelMatrix[2][3] = 0;
          transformMatrix   = MatMul(vpMatrix, modelMatrix);
          drawBox(transformMatrix, textures[0], cube, sController, windowId, lightDir);
        }
      }
    }

    // Calcul du FPS (mise à jour chaque seconde)
    frameCount++;
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<float>(now - lastFpsUpdate).count();
    if (elapsed >= 1.0f) {
      fps           = frameCount / elapsed;
      frameCount    = 0;
      lastFpsUpdate = now;
    }

    sController.getWindow(windowId).write(
        "FPS: " + std::to_string(static_cast<int>(fps)), 0, 0);
    sController.getWindow(windowId).write("Rot: " + std::to_string(rot[2]), 0, 4);
    sController.getWindow(windowId).write("x: " + std::to_string(x), 0, 5);
    sController.getWindow(windowId).write("y: " + std::to_string(y), 0, 6);

    sController.getWindow(windowId).refresh();

    // Sleep pour atteindre ~60 FPS
    auto frameEnd = std::chrono::steady_clock::now();
    auto frameDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart)
            .count();
    long sleepTime = (1000 / 60) - frameDuration;
    if (sleepTime > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
    }
  }
  return 0;
}
