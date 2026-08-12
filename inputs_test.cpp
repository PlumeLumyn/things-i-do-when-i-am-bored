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
  Vec2f uv;
};

struct Image {
  int width;
  int height;
  int channels;
  unsigned char *data;
};

Image cobblestoneTexture;

void drawLine(Screen::Window &w, Vec4f o, Vec4f d,
              Vec3f col = {1.0, 1.0, 1.0}) {
  Vec2i oi = ToScreenCoords(o, w.getWidth(), w.getSubPixelHeight());
  Vec2i di = ToScreenCoords(d, w.getWidth(), w.getSubPixelHeight());
  int x0 = oi[0], y0 = oi[1];
  int x1 = di[0], y1 = di[1];

  int dx = abs(x1 - x0);
  int dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  int steps = std::max(abs(x1 - x0), abs(y1 - y0));
  double z = o[2] * o[3];
  double dz = steps > 0 ? (d[2] * d[3] - o[2] * o[3]) / steps : 0;

  while (true) {
    w.putPixel(x0, y0, z, col);

    if (x0 == x1 && y0 == y1)
      break;

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

void drawTriangle(Screen::Window &w, Vec4f v0, Vec4f v1, Vec4f v2, Vec2f uv0,
                  Vec2f uv1, Vec2f uv2,
                  Vec3f (*shader)(Vec3f pos, Vec2f uv) = nullptr) {
  Vec2i p0 = ToScreenCoords(v0, w.getWidth(), w.getSubPixelHeight());
  Vec2i p1 = ToScreenCoords(v1, w.getWidth(), w.getSubPixelHeight());
  Vec2i p2 = ToScreenCoords(v2, w.getWidth(), w.getSubPixelHeight());

  // Bounding box
  int minX = std::max(0, std::min({p0[0], p1[0], p2[0]}));
  int maxX = std::min(w.getWidth() - 1, std::max({p0[0], p1[0], p2[0]}));
  int minY = std::max(0, std::min({p0[1], p1[1], p2[1]}));
  int maxY =
      std::min(w.getSubPixelHeight() - 1, std::max({p0[1], p1[1], p2[1]}));

  // Precompute for barycentric
  float denom =
      (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1]);
  if (std::abs(denom) < 1e-6f)
    return; // Degenerate triangle

  float invDenom = 1.0f / denom;

  // Perspective-correct interpolation: store 1/w
  float invW0 = 1.0f / v0[3];
  float invW1 = 1.0f / v1[3];
  float invW2 = 1.0f / v2[3];

  // Depth values (z/w for depth buffer)
  float z0 = v0[2] * invW0;
  float z1 = v1[2] * invW1;
  float z2 = v2[2] * invW2;

  // UV divided by w for perspective-correct interpolation
  Vec2f uv0_w = uv0 * invW0;
  Vec2f uv1_w = uv1 * invW1;
  Vec2f uv2_w = uv2 * invW2;

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      // Barycentric coordinates
      float w0 =
          ((p1[1] - p2[1]) * (x - p2[0]) + (p2[0] - p1[0]) * (y - p2[1])) *
          invDenom;
      float w1 =
          ((p2[1] - p0[1]) * (x - p2[0]) + (p0[0] - p2[0]) * (y - p2[1])) *
          invDenom;
      float w2 = 1.0f - w0 - w1;

      // Check if inside triangle
      if (w0 < 0 || w1 < 0 || w2 < 0)
        continue;

      // Interpolate depth
      float z = w0 * z0 + w1 * z1 + w2 * z2;

      // Perspective-correct UV interpolation
      float invW = w0 * invW0 + w1 * invW1 + w2 * invW2;
      Vec2f uv = (w0 * uv0_w + w1 * uv1_w + w2 * uv2_w) / invW;

      Vec3f fragColor = {1.0f, 1.0f, 1.0f};
      if (shader) {
        fragColor =
            shader({static_cast<float>(x), static_cast<float>(y), z}, uv);
      };

      w.putPixel(x, y, -z, fragColor);
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
  cobblestoneTexture.width = texWidth;
  cobblestoneTexture.height = texHeight;
  cobblestoneTexture.channels = texChannels;
  cobblestoneTexture.data = data;

  // All triangles of the cube (two per face so 12*3 = 36 vertices)
  std::array<Vertex, 36> cube = {
      -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 0.0f,
      0.5f,  0.5f,  -0.5f, 1.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
      -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,

      -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
      0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
      -0.5f, 0.5f,  0.5f,  0.0f, 1.0f, -0.5f, -0.5f, 0.5f,  0.0f, 0.0f,

      -0.5f, 0.5f,  0.5f,  1.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 1.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
      -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  0.5f,  1.0f, 0.0f,

      0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
      0.5f,  -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 0.0f, 1.0f,
      0.5f,  -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

      -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 1.0f,
      0.5f,  -0.5f, 0.5f,  1.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
      -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,

      -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
      0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
      -0.5f, 0.5f,  0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f};

  Vec3f camera = {0.0, 0.0, 5.0};
  Vec3f cameraAngle = {0.0, 0.0, 0.0};
  Vec3f vel = {0.0, 0.0, 0.0};
  Vec3f gravity = {0.0, -0.05, 0.0};
  Mat4f viewMatrix;
  Identity(viewMatrix);
  Mat4f projectionMatrix;
  Identity(projectionMatrix);
  Mat4f modelMatrix;
  Identity(modelMatrix);
  bool reversedGravity = false;

  // Variables pour le calcul du FPS
  float fps = 0.0f;
  auto lastFpsUpdate = std::chrono::steady_clock::now();
  int frameCount = 0;

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
      vel[1] = 0.0;
      camera[1] = 5.0;
    }

    size_t windowId = sController.createWindow(0, 0, 0, 0);
    sController.getWindow(windowId).clear();
    const float NEAR_CLIP = 0.01;
    const float FAR_CLIP = 20.0;

    viewMatrix = BuildViewMatrix(camera, cameraAngle);
    float fovY = 90.0f * (M_PI / 180.0f); // 30 degrees in radians
    projectionMatrix = BuildProjectionMatrix(
        fovY,
        static_cast<float>(sController.getWindow(windowId).getWidth()) /
            static_cast<float>(
                sController.getWindow(windowId).getSubPixelHeight()),
        NEAR_CLIP, FAR_CLIP);

    Mat4f vpMatrix = MatMul(projectionMatrix, viewMatrix);
    for (int i = -10; i <= 10; i++)
      for (int j = -10; j <= 10; j++) {
        Identity(modelMatrix);
        modelMatrix[0][0] *= 2.0f;
        modelMatrix[1][1] *= 2.0f;
        modelMatrix[2][2] *= 2.0f;
        modelMatrix[0][3] += i * 2;
        modelMatrix[2][3] += j * 2;
        Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
        for (size_t k = 0; k < cube.size() / 3; k++) {
          std::array<Vec3f, 3> v;
          std::array<Vec2f, 3> uv;
          std::array<Vec4f, 3> t;
          v[0] = cube[k * 3].pos;
          v[1] = cube[k * 3 + 1].pos;
          v[2] = cube[k * 3 + 2].pos;
          uv[0] = cube[k * 3].uv;
          uv[1] = cube[k * 3 + 1].uv;
          uv[2] = cube[k * 3 + 2].uv;
          t[0] = ProjectPoint(v[0], transformMatrix);
          t[1] = ProjectPoint(v[1], transformMatrix);
          t[2] = ProjectPoint(v[2], transformMatrix);
          // After perspective divide, Z is in NDC range -1 to +1
          // Also check W was positive (point was in front of camera)
          if (t[0][3] <= 0 || t[1][3] <= 0 || t[2][3] <= 0) // Behind camera
            continue;
          if (t[0][2] < -1.0f || t[0][2] > 1.0f || t[1][2] < -1.0f ||
              t[1][2] > 1.0f || t[2][2] < -1.0f || t[2][2] > 1.0f)
            continue;
          drawTriangle(
              sController.getWindow(windowId), t[0], t[1], t[2], uv[0], uv[1],
              uv[2], // UV coords per vertex
              [](Vec3f pos, Vec2f uv) {
                int texX =
                    static_cast<int>(uv[0] * (cobblestoneTexture.width - 1));
                int texY =
                    static_cast<int>(uv[1] * (cobblestoneTexture.height - 1));
                int texIndex = (texY * cobblestoneTexture.width + texX) *
                               cobblestoneTexture.channels;
                Vec3f fragColor = {
                    cobblestoneTexture.data[texIndex] / 255.0f,
                    cobblestoneTexture.data[texIndex + 1] / 255.0f,
                    cobblestoneTexture.data[texIndex + 2] / 255.0f};
                fragColor *= pos[2] * 3.0f;
                return fragColor;
              }); // base color
        }
      }
    // Calcul du FPS (mise à jour chaque seconde)
    frameCount++;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<float>(now - lastFpsUpdate).count();
    if (elapsed >= 1.0f) {
      fps = frameCount / elapsed;
      frameCount = 0;
      lastFpsUpdate = now;
    }

    sController.getWindow(windowId).write(
        "FPS: " + std::to_string(static_cast<int>(fps)), 0, 0);
    sController.getWindow(windowId).refresh();

    // Sleep pour atteindre ~60 FPS
    auto frameEnd = std::chrono::steady_clock::now();
    auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             frameEnd - frameStart)
                             .count();
    long sleepTime = (1000 / 60) - frameDuration;
    if (sleepTime > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
    }
  }
  return 0;
}
