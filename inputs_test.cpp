#include "screen.hpp"
#include <cstddef>
#include <functional>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "vec.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#define INPUTS_HPP_IMPLEMENTATION
#include "inputs.hpp"

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

Image cobblestoneTexture;

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

void drawTriangle(
    Screen::Window &window, std::array<Vertex, 3> &vertexData, Mat4f &transformMatrix,
    std::function<Vec3f(Vertex)> shader = nullptr) {
  std::array<Vec4f, 3> t;
  t[0] = ProjectPoint(vertexData[0].pos, transformMatrix);
  t[1] = ProjectPoint(vertexData[1].pos, transformMatrix);
  t[2] = ProjectPoint(vertexData[2].pos, transformMatrix);
  // After perspective divide, Z is in NDC range -1 to +1
  // Also check W was positive (point was in front of camera)
  if (t[0][3] <= 0 || t[1][3] <= 0 || t[2][3] <= 0) // Behind camera
    return;
  if (t[0][2] < -1.0f || t[0][2] > 1.0f || t[1][2] < -1.0f || t[1][2] > 1.0f ||
      t[2][2] < -1.0f || t[2][2] > 1.0f)
    return;

  Vec2i p0 = ToScreenCoords(t[0], window.getWidth(), window.getSubPixelHeight());
  Vec2i p1 = ToScreenCoords(t[1], window.getWidth(), window.getSubPixelHeight());
  Vec2i p2 = ToScreenCoords(t[2], window.getWidth(), window.getSubPixelHeight());

  // Bounding box
  int minX = std::max(0, std::min({ p0[0], p1[0], p2[0] }));
  int maxX = std::min(window.getWidth() - 1, std::max({ p0[0], p1[0], p2[0] }));
  int minY = std::max(0, std::min({ p0[1], p1[1], p2[1] }));
  int maxY = std::min(window.getSubPixelHeight() - 1, std::max({ p0[1], p1[1], p2[1] }));

  // Precompute for barycentric
  float denom = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1]);
  if (std::abs(denom) < 1e-6f) return; // Degenerate triangle

  float invDenom = 1.0f / denom;

  std::array<float, 3> invW, z;
  std::array<Vec2f, 3> uv_w;
  std::array<Vec3f, 3> normal_w;
  Vertex v;

  for (size_t i = 0; i < 3; i++) {
    // Perspective-correct interpolation: store 1/w
    invW[i] = 1.0f / t[i][3];

    // Depth values (z/w for depth buffer)
    z[i] = t[i][2] * invW[i];

    // Datas divided by w for perspective-correct interpolation
    uv_w[i]     = vertexData[i].uv * invW[i];
    normal_w[i] = vertexData[i].normal * invW[i];
  }

  auto edgeBias = [](const Vec2i &from, const Vec2i &to) -> float {
    // Top edge: horizontal edge going left (to.x < from.x)
    // Left edge: edge going down (to.y > from.y)
    bool isTop  = (from[1] == to[1]) && (to[0] < from[0]);
    bool isLeft = (to[1] > from[1]);
    // Top-left edges INCLUDE zero (draw on edge), others EXCLUDE zero (skip edge)
    return (isTop || isLeft) ? 0.0f : 1e-6f;
  };

  float bias0 = edgeBias(p1, p2);
  float bias1 = edgeBias(p2, p0);
  float bias2 = edgeBias(p0, p1);

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      std::array<float, 3> w;
      w[0] = ((p1[1] - p2[1]) * (x - p2[0]) + (p2[0] - p1[0]) * (y - p2[1])) * invDenom;
      w[1] = ((p2[1] - p0[1]) * (x - p2[0]) + (p0[0] - p2[0]) * (y - p2[1])) * invDenom;
      w[2] = 1.0f - w[0] - w[1];

      // Reject if outside OR on a non-owned edge
      if (w[0] < bias0 || w[1] < bias1 || w[2] < bias2) continue;

      // Interpolate depth
      float localZ    = w[0] * z[0] + w[1] * z[1] + w[2] * z[2];
      float localInvW = w[0] * invW[0] + w[1] * invW[1] + w[2] * invW[2];

      // Perspective-correct UV interpolation
      v.pos = { static_cast<float>(x), static_cast<float>(y), localZ };
      v.uv  = (w[0] * uv_w[0] + w[1] * uv_w[1] + w[2] * uv_w[2]) / localInvW;
      v.normal =
          (w[0] * normal_w[0] + w[1] * normal_w[1] + w[2] * normal_w[2]) / localInvW;

      Vec3f fragColor = { 1.0f, 1.0f, 1.0f };
      if (shader) {
        fragColor = shader(v);
      };

      window.putPixel(x, y, -localZ, fragColor);
    }
  }
}

int main(void) {
  Inputs::InputController iController;
  Screen::ScreenController sController;

  // Load ./cobblestone.png in a matrix with stb image
  int texWidth, texHeight, texChannels;
  unsigned char *data =
      stbi_load("./cobblestone.png", &texWidth, &texHeight, &texChannels, 0);
  if (!data) {
    std::cerr << "Failed to load texture: ./cobblestone.png" << std::endl;
    return -1;
  }
  cobblestoneTexture.width    = texWidth;
  cobblestoneTexture.height   = texHeight;
  cobblestoneTexture.channels = texChannels;
  cobblestoneTexture.data     = data;

  // All triangles of the cube (two per face so 12*3 = 36 vertices)
  // Format: x, y, z, nx, ny, nz, u, v
  // clang-format off
  std::array<Vertex, 36> cube = {
      // Back face (normal: 0, 0, -1)
      -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
       0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,
       0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,
      -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f,
      -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
                                             
      // Front face (normal: 0, 0, 1) 
      -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,
       0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,
       0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,
      -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f,
      -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,
                                             
      // Left face (normal: -1, 0, 0) 
      -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,
      -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,
      -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
      -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,
                                             
      // Right face (normal: 1, 0, 0) 
       0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,
       0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f,
       0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,
       0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,
       0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
       0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,
                                             
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

  Vec3f camera      = { 0.0, 0.0, 5.0 };
  Vec3f cameraAngle = { 0.0, 0.0, 0.0 };
  Vec3f vel         = { 0.0, 0.0, 0.0 };
  Vec3f gravity     = { 0.0, -0.05, 0.0 };
  Mat4f viewMatrix;
  Identity(viewMatrix);
  Mat4f projectionMatrix;
  Identity(projectionMatrix);
  Mat4f modelMatrix;
  Identity(modelMatrix);
  bool reversedGravity = false;

  // Variables pour le calcul du FPS
  float fps          = 0.0f;
  auto lastFpsUpdate = std::chrono::steady_clock::now();
  int frameCount     = 0;

  while (1) {
    auto frameStart = std::chrono::steady_clock::now();

    if (iController.isPressed(Inputs::Keycode::Z)) {
      vel[2] -= 0.05 * cos(cameraAngle[1]);
      vel[0] -= 0.05 * sin(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::S)) {
      vel[2] += 0.05 * cos(cameraAngle[1]);
      vel[0] += 0.05 * sin(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::Q)) {
      vel[2] -= 0.05 * -sin(cameraAngle[1]);
      vel[0] -= 0.05 * cos(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::D)) {
      vel[2] += 0.05 * -sin(cameraAngle[1]);
      vel[0] += 0.05 * cos(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::A)) {
      cameraAngle[1] += 0.05;
    }
    if (iController.isPressed(Inputs::Keycode::E)) {
      cameraAngle[1] -= 0.05;
    }
    if (iController.isPressed(Inputs::Keycode::Space)) {
      vel[1] += 0.5 - vel[1] * 2.0;
    }
    if (iController.isPressed(Inputs::Keycode::LeftShift)) {
      vel[1] = -0.5;
    }
    // if (iController.isJustPressed(Inputs::Keycode::Space)) {
    //   reversedGravity = !reversedGravity;
    // }
    iController.refresh();

    if (reversedGravity) {
      cameraAngle[2] += (3.1415 - cameraAngle[2]) / 2.0;
      gravity[1] += (0.05 - gravity[1]) / 2.0;
    } else {
      cameraAngle[2] += (0 - cameraAngle[2]) / 2.0;
      gravity[1] += (-0.05 - gravity[1]) / 2.0;
    }

    vel[1] += gravity[1];
    camera[0] += vel[0];
    camera[1] += vel[1];
    camera[2] += vel[2];

    vel[0] *= 0.8;
    vel[1] *= 0.8;
    vel[2] *= 0.8;

    if (camera[1] < 5.0) {
      vel[1]    = 0.0;
      camera[1] = 5.0;
    }

    size_t windowId = sController.createWindow(0, 0, 0, 0);
    sController.getWindow(windowId).clear();
    const float NEAR_CLIP = 0.01;
    const float FAR_CLIP  = 20.0;

    viewMatrix       = BuildViewMatrix(camera, cameraAngle);
    float fovY       = 90.0f * (M_PI / 180.0f); // 30 degrees in radians
    projectionMatrix = BuildProjectionMatrix(
        fovY,
        static_cast<float>(sController.getWindow(windowId).getWidth()) /
            static_cast<float>(sController.getWindow(windowId).getSubPixelHeight()),
        NEAR_CLIP,
        FAR_CLIP);

    Mat4f vpMatrix = MatMul(projectionMatrix, viewMatrix);
    Identity(modelMatrix);
    modelMatrix[0][0] *= 20.0f;
    modelMatrix[1][1] *= 2.0f;
    modelMatrix[2][2] *= 20.0f;
    Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
    for (size_t k = 0; k < cube.size() / 3; k++) {
      std::array<Vertex, 3> vertexData;
      vertexData[0]     = cube[k * 3];
      vertexData[1]     = cube[k * 3 + 1];
      vertexData[2]     = cube[k * 3 + 2];
      Mat3f viewMatrix3 = {
        viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0],
        viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1],
        viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]
      };
      Vec3f lightDir;
      MatrixVectorMult(viewMatrix3, { 0.0f, 0.0f, 1.0f }, lightDir);
      float light = DotProduct(Normalize(vertexData[0].normal), Normalize(lightDir));
      if (light >= 0)
        drawTriangle(
            sController.getWindow(windowId),
            vertexData,
            transformMatrix,
            [light](Vertex v) {
              int texX = static_cast<int>(v.uv[0] * (cobblestoneTexture.width - 1));
              int texY = static_cast<int>(v.uv[1] * (cobblestoneTexture.height - 1));
              int texIndex =
                  (texY * cobblestoneTexture.width + texX) * cobblestoneTexture.channels;
              Vec3f fragColor = {
                cobblestoneTexture.data[texIndex] / 255.0f,
                cobblestoneTexture.data[texIndex + 1] / 255.0f,
                cobblestoneTexture.data[texIndex + 2] / 255.0f
              };
              fragColor *= v.pos[2] * (3.0f + light * 3.0f);
              return fragColor;
            }); // base color
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
