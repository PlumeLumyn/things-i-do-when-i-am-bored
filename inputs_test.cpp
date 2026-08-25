#include "noise.hpp"
#include "screen.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
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

struct BlockData {
    const Image &top, &left, &right, &front, &back, &bottom;
    const Image &operator[](const int id) const {
      switch (id) {
      case 0:
        return back;
      case 1:
        return front;
      case 2:
        return left;
      case 3:
        return right;
      case 4:
        return bottom;
      case 5:
        return top;
      }
      return top;
    }
};

struct Block {
    int id = 0;
    Vec3i position;
    Block(int id, Vec3i position) : id(id), position(position) {};

    bool operator==(const Block &o) {
      return id == o.id && position == o.position;
    }

    bool operator>(const Block &o) {
      return id > o.id && position > o.position;
    }
};

using Chunk           = std::vector<Block>;
using ChunkCollection = std::unordered_map<Vec3i, Chunk>;

constexpr int WORLD_WIDTH     = 256;
constexpr int WORLD_DEPTH     = 256;
constexpr int WORLD_HEIGHT    = 80;
constexpr int DIRT_SIZE       = 3;
constexpr float CLIFF_PERCENT = 0.5f;
constexpr float CLIFF_RADIUS  = 32.0f;
constexpr int CHUNK_SIZE      = 16;

Image loadTexture(const std::string &path) {
  Image tex;
  int texWidth, texHeight, texChannels;
  unsigned char *data = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, 0);
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

Chunk &getChunkFromCollection(ChunkCollection &collection, const Vec3i &position) {
  return collection[{
      floorDiv(position[0], CHUNK_SIZE),
      floorDiv(position[1], CHUNK_SIZE),
      floorDiv(position[2], CHUNK_SIZE) }];
}

// Add this helper function
inline int positiveMod(int value, int mod) {
  int result = value % mod;
  return result < 0 ? result + mod : result;
}

Vec3i getBlockChunkPosition(const Block &block) {
  return {
    positiveMod(static_cast<int>(block.position[0]), CHUNK_SIZE),
    positiveMod(static_cast<int>(block.position[1]), CHUNK_SIZE),
    positiveMod(static_cast<int>(block.position[2]), CHUNK_SIZE)
  };
}

bool isPositionInChunk(const Vec3i &position) {
  return position[0] >= 0 && position[0] < CHUNK_SIZE && position[1] >= 0 &&
         position[1] < CHUNK_SIZE && position[2] >= 0 && position[2] < CHUNK_SIZE;
}

void addBlockToChunkCollection(ChunkCollection &collection, const Block &block) {
  getChunkFromCollection(
      collection,
      Vec3i { static_cast<int>(block.position[0]),
              static_cast<int>(block.position[1]),
              static_cast<int>(block.position[2]) })
      .push_back(block);
}

void removeBlockToChunkCollection(ChunkCollection &collection, const Block &block) {
  auto &blocks = getChunkFromCollection(
      collection,
      Vec3i { static_cast<int>(block.position[0]),
              static_cast<int>(block.position[1]),
              static_cast<int>(block.position[2]) });
  blocks.erase(std::remove(blocks.begin(), blocks.end(), block), blocks.end());
}

bool rayIntersectsBox(Vec3f origin, Vec3f dir, Vec3f boxMin, Vec3f boxMax) {
  float tMin = -std::numeric_limits<float>::infinity();
  float tMax = std::numeric_limits<float>::infinity();

  for (size_t i = 0; i <= 2; i++) {
    // iterate X, Y, Z axes
    if (dir[i] == 0) {
      if (origin[i] < boxMin[i] || origin[i] > boxMax[i]) {
        return false;
      }
    } else {
      float t1 = (boxMin[i] - origin[i]) / dir[i];
      float t2 = (boxMax[i] - origin[i]) / dir[i];

      float tEnter = std::min(t1, t2);
      float tLeave = std::max(t1, t2);

      tMin = std::max(tMin, tEnter);
      tMax = std::min(tMax, tLeave);

      if (tMin > tMax || tMax < 0) return false;
    }
  }
  return true;
}

Vec3f getIntersectionPoint(Vec3f rayOrigin, Vec3f rayDir, Vec3f boxMin, Vec3f boxMax) {
  float tMin = 0.0f;
  float tMax = std::numeric_limits<float>::infinity();

  // For each axis (X, Y, Z)
  for (int i = 0; i < 3; i++) {
    float origin = ((float *)&rayOrigin)[i];
    float dir    = ((float *)&rayDir)[i];
    float bMin   = ((float *)&boxMin)[i];
    float bMax   = ((float *)&boxMax)[i];

    if (abs(dir) < 1e-8f) {
      // Ray is parallel to the slab
      if (origin < bMin || origin > bMax) {
        return rayOrigin; // No intersection
      }
    } else {
      // Compute intersection t values
      float t1 = (bMin - origin) / dir;
      float t2 = (bMax - origin) / dir;

      if (t1 > t2) {
        float temp = t1;
        t1         = t2;
        t2         = temp;
      }

      tMin = fmax(tMin, t1);
      tMax = fmin(tMax, t2);

      if (tMin > tMax) {
        return rayOrigin; // No intersection
      }
    }
  }

  // Return the intersection point at tMin (entry point)
  return rayOrigin + rayDir * tMin;
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

void drawTriangle(
    Screen::Window &window, std::array<Vertex, 3> &vertexData, Mat4f &transformMatrix,
    Shader shader = nullptr, Vec2f offset = { 0.0f, 0.0f }) {
  // Transform to clip space (NO perspective divide yet)
  std::array<Vec4f, 3> clipSpace;
  clipSpace[0] = ToClipSpace(vertexData[0].pos, transformMatrix);
  clipSpace[1] = ToClipSpace(vertexData[1].pos, transformMatrix);
  clipSpace[2] = ToClipSpace(vertexData[2].pos, transformMatrix);

  // Start with original triangle
  std::vector<Vertex> vertices     = { vertexData[0], vertexData[1], vertexData[2] };
  std::vector<Vec4f> clipPositions = { clipSpace[0], clipSpace[1], clipSpace[2] };

  // Clip against all 6 frustum planes
  for (int plane = 0; plane < 6; plane++) {
    if (vertices.size() < 3) return; // Completely clipped
    clipAgainstPlane(vertices, clipPositions, plane);
  }

  if (vertices.size() < 3) return;

  for (size_t i = 1; i + 1 < vertices.size(); i++) {
    std::array<Vertex, 3> tri  = { vertices[0], vertices[i], vertices[i + 1] };
    std::array<Vec4f, 3> tClip = {
      clipPositions[0], clipPositions[i], clipPositions[i + 1]
    };

    // NOW do perspective divide to get NDC
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
    int maxY =
        std::min(window.getSubPixelHeight() - 1, std::max({ p0[1], p1[1], p2[1] }));

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
      return (isTop || isLeft) ? 0.0f : -1e-6f;
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
}

const std::array textures = {
  loadTexture("./textures/cobblestone.png"),
  loadTexture("./textures/grass_block_top.png"),
  loadTexture("./textures/grass_block_side.png"),
  loadTexture("./textures/dirt.png"),
  loadTexture("./textures/oak_log.png"),
  loadTexture("./textures/oak_log_top.png"),
  loadTexture("./textures/oak_planks.png"),
};
const std::array blockDatas = {
  BlockData {
             .top    = textures[0],
             .left   = textures[0],
             .right  = textures[0],
             .front  = textures[0],
             .back   = textures[0],
             .bottom = textures[0],
             },
  BlockData {
             .top    = textures[1],
             .left   = textures[2],
             .right  = textures[2],
             .front  = textures[2],
             .back   = textures[2],
             .bottom = textures[3],
             },
  BlockData {
             .top    = textures[3],
             .left   = textures[3],
             .right  = textures[3],
             .front  = textures[3],
             .back   = textures[3],
             .bottom = textures[3],
             },
  BlockData {
             .top    = textures[6],
             .left   = textures[6],
             .right  = textures[6],
             .front  = textures[6],
             .back   = textures[6],
             .bottom = textures[6],
             },
  BlockData {
             .top    = textures[5],
             .left   = textures[4],
             .right  = textures[4],
             .front  = textures[4],
             .back   = textures[4],
             .bottom = textures[5],
             },
};

const std::array blockDisplaces {
  Vec3i { 0,  0,  -1 },
   Vec3i { 0,  0,  1  },
   Vec3i { -1, 0,  0  },
  Vec3i { 1,  0,  0  },
   Vec3i { 0,  -1, 0  },
   Vec3i { 0,  1,  0  },
};

ChunkCollection chunks;

int main(void) {
  Inputs::InputController iController;
  Screen::ScreenController sController;

  for (int i = 0; i <= WORLD_WIDTH; i++)
    for (int j = 0; j <= WORLD_DEPTH; j++) {
      Vec2f uv    = Vec2f { i / CLIFF_RADIUS, j / CLIFF_RADIUS };
      float noise = snoise(uv);
      float depth = noise * (CLIFF_PERCENT / 2.0f) + (1.0f - CLIFF_PERCENT / 2.0f);
      for (int z = 0; z < depth * WORLD_HEIGHT; z++) {
        int id = 0;

        if (z + 1 >= depth * WORLD_HEIGHT) {
          id = 1;
        } else if (z + DIRT_SIZE >= depth * WORLD_HEIGHT) {
          id = 2;
        }
        addBlockToChunkCollection(
            chunks,
            {
                id,
                Vec3i {
                       (i - WORLD_WIDTH / 2), (z - WORLD_HEIGHT), (j - WORLD_DEPTH / 2) }
        });
      }
    }

  // All triangles of the cube (two per face so 12*3 = 36 vertices)
  // Format: x, y, z, nx, ny, nz, u, v
  // clang-format off
  std::array<Vertex, 36> cube = {
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

  Vec3f camera       = { 0.0f, 0.0f, 0.0f };
  Vec3f pos          = { 0.0f, 0.5f, 0.0f };
  Vec3f cameraAngle  = { 0.0, 0.0, 0.0 };
  Vec3f vel          = { 0.0, 0.0, 0.0 };
  Vec3f gravity      = { 0.0, -0.04f, 0.0 };
  Vec2i lastMousePos = { -1, -1 };
  Mat4f viewMatrix;
  size_t id = 0;
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
  bool canJump       = true;
  bool isJumping     = false; // Track if we're in a jump
  int jumpFrames     = 0;     // Track how long jump has been held

  while (1) {
    auto frameStart = std::chrono::steady_clock::now();

    float SPEED = 0.04f;
    camera[1]   = 1.5f;
    if (iController.isPressed(Inputs::Keycode::C)) {
      SPEED     = 0.01f;
      camera[1] = 1.3f;
    }
    if (iController.isPressed(Inputs::Keycode::Z)) {
      vel[2] -= SPEED * cos(cameraAngle[1]);
      vel[0] -= SPEED * sin(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::S)) {
      vel[2] += SPEED * cos(cameraAngle[1]);
      vel[0] += SPEED * sin(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::Q)) {
      vel[2] -= SPEED * -sin(cameraAngle[1]);
      vel[0] -= SPEED * cos(cameraAngle[1]);
    }
    if (iController.isPressed(Inputs::Keycode::D)) {
      vel[2] += SPEED * -sin(cameraAngle[1]);
      vel[0] += SPEED * cos(cameraAngle[1]);
    }
    if (iController.isJustPressed(Inputs::Keycode::R)) {
      id += 1;
      if (id >= blockDatas.size()) {
        id = 0;
      }
    }
    bool actBreak = false;
    bool actPlace = false;
    if (iController.isJustPressed(Inputs::Keycode::MouseLeft)) {
      actBreak = true;
    }
    if (iController.isJustPressed(Inputs::Keycode::MouseRight)) {
      actPlace = true;
    }
    if (lastMousePos[0] != -1) {
      int xMove = iController.getMousePos()[0] - lastMousePos[0];
      int yMove = iController.getMousePos()[1] - lastMousePos[1];
      cameraAngle[1] += static_cast<float>(-xMove) / 100.0f;
      cameraAngle[0] += static_cast<float>(-yMove) / 100.0f;
      if (cameraAngle[0] < -1.57) cameraAngle[0] = -1.57;
      if (cameraAngle[0] > 1.57) cameraAngle[0] = 1.57;
    }
    lastMousePos = iController.getMousePos();
    if (iController.isJustPressed(Inputs::Keycode::Space) && canJump && vel[1] > -0.2f) {
      // Initial jump impulse
      vel[1]     = 0.2f;
      canJump    = false;
      isJumping  = true;
      jumpFrames = 0;
    }
    // Continue adding upward velocity while holding Space (limited duration and only
    // while moving up)
    const int MAX_JUMP_FRAMES = 8;  // Maximum frames to allow boost (adjust for max jump
                                    // height)
    const float JUMP_BOOST = 0.05f; // Additional velocity per frame while holding
                                    //
    if (iController.isPressed(Inputs::Keycode::Space) && isJumping &&
        jumpFrames < MAX_JUMP_FRAMES && vel[1] > 0) {
      vel[1] += JUMP_BOOST;
      jumpFrames++;
    } else if (!iController.isPressed(Inputs::Keycode::Space) || vel[1] <= 0) {
      // Stop boosting if Space released or player starts falling
      isJumping = false;
    }

    iController.refresh();

    vel += gravity;
    pos += vel;

    vel *= 0.9f;

    // if (pos[1] < 0.5f) {
    //   vel[1] = 0.0f;
    //   pos[1] = 0.5f;
    // }

    size_t windowId = sController.createWindow(0, 0, 0, 0);
    sController.getWindow(windowId).clear();
    const float NEAR_CLIP = 0.0001;
    const float FAR_CLIP  = 20.0;
    const float RATIO =
        static_cast<float>(sController.getWindow(windowId).getWidth()) /
        static_cast<float>(sController.getWindow(windowId).getSubPixelHeight());

    viewMatrix       = BuildViewMatrix(pos + camera, cameraAngle);
    float fovY       = 80.0f * (M_PI / 180.0f); // 30 degrees in radians
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
    const Block *closestBlock = nullptr;
    const std::array displaces {
      Vec3i { 0,           0,           0           },
      Vec3i { 0,           0,           -CHUNK_SIZE },
      Vec3i { 0,           0,           CHUNK_SIZE  },
      Vec3i { 0,           -CHUNK_SIZE, 0           },
      Vec3i { 0,           -CHUNK_SIZE, -CHUNK_SIZE },
      Vec3i { 0,           -CHUNK_SIZE, CHUNK_SIZE  },
      Vec3i { 0,           CHUNK_SIZE,  0           },
      Vec3i { 0,           CHUNK_SIZE,  -CHUNK_SIZE },
      Vec3i { 0,           CHUNK_SIZE,  CHUNK_SIZE  },
      Vec3i { -CHUNK_SIZE, 0,           0           },
      Vec3i { -CHUNK_SIZE, 0,           -CHUNK_SIZE },
      Vec3i { -CHUNK_SIZE, 0,           CHUNK_SIZE  },
      Vec3i { -CHUNK_SIZE, -CHUNK_SIZE, 0           },
      Vec3i { -CHUNK_SIZE, -CHUNK_SIZE, -CHUNK_SIZE },
      Vec3i { -CHUNK_SIZE, -CHUNK_SIZE, CHUNK_SIZE  },
      Vec3i { -CHUNK_SIZE, CHUNK_SIZE,  0           },
      Vec3i { -CHUNK_SIZE, CHUNK_SIZE,  -CHUNK_SIZE },
      Vec3i { -CHUNK_SIZE, CHUNK_SIZE,  CHUNK_SIZE  },
      Vec3i { CHUNK_SIZE,  0,           0           },
      Vec3i { CHUNK_SIZE,  0,           -CHUNK_SIZE },
      Vec3i { CHUNK_SIZE,  0,           CHUNK_SIZE  },
      Vec3i { CHUNK_SIZE,  -CHUNK_SIZE, 0           },
      Vec3i { CHUNK_SIZE,  -CHUNK_SIZE, -CHUNK_SIZE },
      Vec3i { CHUNK_SIZE,  -CHUNK_SIZE, CHUNK_SIZE  },
      Vec3i { CHUNK_SIZE,  CHUNK_SIZE,  0           },
      Vec3i { CHUNK_SIZE,  CHUNK_SIZE,  -CHUNK_SIZE },
      Vec3i { CHUNK_SIZE,  CHUNK_SIZE,  CHUNK_SIZE  },
    };
    size_t blockCount = 0;
    for (const auto &displace : displaces) {
      blockCount +=
          getChunkFromCollection(
              chunks,
              Vec3i { static_cast<int>(pos[0]),
                      static_cast<int>(pos[1]),
                      static_cast<int>(pos[2]) } +
                  displace)
              .size();
    }
    for (const auto &displace : displaces) {
      std::array<std::array<std::array<bool, CHUNK_SIZE>, CHUNK_SIZE>, CHUNK_SIZE>
          blockGrid {};
      Chunk &chunk = getChunkFromCollection(
          chunks,
          Vec3i { static_cast<int>(pos[0]),
                  static_cast<int>(pos[1]),
                  static_cast<int>(pos[2]) } +
              displace);
      for (const auto &block : chunk) {
        const Vec3i &blockChunkPosition = getBlockChunkPosition(block);
        blockGrid[blockChunkPosition[0]][blockChunkPosition[1]][blockChunkPosition[2]] =
            true;
      }
      for (const auto &block : chunk) {
        modelMatrix[0][3]        = block.position[0];
        modelMatrix[1][3]        = block.position[1];
        modelMatrix[2][3]        = block.position[2];
        Mat4f transformMatrix    = MatMul(vpMatrix, modelMatrix);
        Vec3f floatBlockPosition = Vec3f {
          static_cast<float>(block.position[0]),
          static_cast<float>(block.position[1]),
          static_cast<float>(block.position[2])
        };
        Vec3f blockP1 = floatBlockPosition - Vec3f { 0.5f, 0.5f, 0.5f };
        Vec3f blockP2 = floatBlockPosition + Vec3f { 0.5f, 0.5f, 0.5f };
        Vec3f posP1   = pos - Vec3f { 0.3f, 0.0f, 0.3f };
        Vec3f posP2   = pos + Vec3f { 0.3f, 1.9f, 0.3f };
        if (posP1[0] <= blockP2[0] && posP1[1] <= blockP2[1] && posP1[2] <= blockP2[2] &&
            posP2[0] >= blockP1[0] && posP2[1] >= blockP1[1] && posP2[2] >= blockP1[2]) {
          // Calculate overlap on each axis
          float overlapX = std::min(posP2[0] - blockP1[0], blockP2[0] - posP1[0]);
          float overlapY = std::min(posP2[1] - blockP1[1], blockP2[1] - posP1[1]);
          float overlapZ = std::min(posP2[2] - blockP1[2], blockP2[2] - posP1[2]);

          float restitution = 0.0;

          // Find the axis with minimum overlap (that's the collision normal)
          if (overlapX < overlapY && overlapX < overlapZ) {
            // Resolve along X axis
            float direction = (pos[0] < block.position[0]) ? -1.0f : 1.0f;
            pos[0] += direction * overlapX;
            vel[0] = -vel[0] * restitution - gravity[0]; // or apply bounce: vel[0] =
                                                         // -vel[0] * restitution;
          } else if (overlapY < overlapZ) {
            // Resolve along Y axis
            float direction = (pos[1] < block.position[1]) ? -1.0f : 1.0f;
            pos[1] += direction * overlapY;
            vel[1] = -vel[1] * restitution - gravity[1];
            if (direction == 1.0f) {
              canJump = true;
              // Ground friction is harder
              vel *= 0.9f;
            }
          } else {
            // Resolve along Z axis
            float direction = (pos[2] < block.position[2]) ? -1.0f : 1.0f;
            pos[2] += direction * overlapZ;
            vel[2] = -vel[2] * restitution - gravity[2];
          }
        }
        if (rayIntersectsBox(
                pos + camera, Vec3f { 0.0f, 0.0f, 0.0f } - lightDir, blockP1, blockP2)) {
          if (!closestBlock ||
              (Magnitude2(pos + camera - floatBlockPosition) <
               Magnitude2(
                   pos + camera -
                   Vec3f { static_cast<float>(closestBlock->position[0]),
                           static_cast<float>(closestBlock->position[1]),
                           static_cast<float>(closestBlock->position[2]) })))
            closestBlock = &block;
        }
        for (size_t k = 0; k < cube.size() / 3; k++) {
          std::array<Vertex, 3> vertexData;
          vertexData[0] = cube[k * 3];
          vertexData[1] = cube[k * 3 + 1];
          vertexData[2] = cube[k * 3 + 2];
          const Vec3i blockChunkPosition =
              getBlockChunkPosition(block) + blockDisplaces[k / 2];
          if (isPositionInChunk(blockChunkPosition) &&
              blockGrid
                  [blockChunkPosition[0]][blockChunkPosition[1]][blockChunkPosition[2]])
            continue;
          const Image &tex = blockDatas[block.id][k / 2];
          float light      = DotProduct(Normalize(vertexData[0].normal), lightDir);
          drawTriangle(
              sController.getWindow(windowId),
              vertexData,
              transformMatrix,
              [&tex, &light](Vertex v, Vec3f &fragColor) {
                int texX     = static_cast<int>(v.uv[0] * (tex.width)) % tex.width;
                int texY     = static_cast<int>(v.uv[1] * (tex.height)) % tex.height;
                int texIndex = (texY * tex.width + texX) * tex.channels;
                fragColor    = {
                  tex.data[texIndex] / 255.0f,
                  tex.data[texIndex + 1] / 255.0f,
                  tex.data[texIndex + 2] / 255.0f
                };
                float lightPow = (v.pos[2]) * (1.0f + light);
                fragColor *= lightPow;
                fragColor += Vec3f { 0.6f, 0.6f, 0.9f } * std::max(0.0f, 0.5f - lightPow);

                // float lightPow = snoise(v.uv * 4.0f) * .5 + .5;
                // fragColor      = { lightPow, lightPow, lightPow };

                if (fragColor[0] > 1.0f) fragColor[0] = 1.0f;
                if (fragColor[1] > 1.0f) fragColor[1] = 1.0f;
                if (fragColor[2] > 1.0f) fragColor[2] = 1.0f;
                if (fragColor[0] < 0.0f) fragColor[0] = 0.0f;
                if (fragColor[1] < 0.0f) fragColor[1] = 0.0f;
                if (fragColor[2] < 0.0f) fragColor[2] = 0.0f;

                return true;
              }); // base color
        }
      }
    }
    if (closestBlock) {
      modelMatrix[0][3]     = closestBlock->position[0];
      modelMatrix[1][3]     = closestBlock->position[1];
      modelMatrix[2][3]     = closestBlock->position[2];
      Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
      for (size_t k = 0; k < cube.size() / 3; k++) {
        std::array<Vertex, 3> vertexData;
        vertexData[0] = cube[k * 3];
        vertexData[1] = cube[k * 3 + 1];
        vertexData[2] = cube[k * 3 + 2];

        drawTriangle(
            sController.getWindow(windowId),
            vertexData,
            transformMatrix,
            [](Vertex v, Vec3f &fragColor) {
              fragColor = { 0.0f, 0.0f, 0.0f };
              int x     = static_cast<int>(v.uv[0] * (16)) % 16;
              int y     = static_cast<int>(v.uv[1] * (16)) % 16;
              if (x > 0 && y > 0 && x < 15 && y < 15) return false;
              return true;
            }); // base color
      }

      if (actPlace) {
        // Determine which face was hit by checking the ray-box intersection point
        Vec3f rayOrigin = pos + camera;
        Vec3f rayDir    = Vec3f { 0.0f, 0.0f, 0.0f } - lightDir;

        // Get the intersection point
        Vec3f floatBlockPosition = {
          static_cast<float>(closestBlock->position[0]),
          static_cast<float>(closestBlock->position[1]),
          static_cast<float>(closestBlock->position[2])
        };
        Vec3f blockMin = floatBlockPosition - Vec3f { 0.5f, 0.5f, 0.5f };
        Vec3f blockMax = floatBlockPosition + Vec3f { 0.5f, 0.5f, 0.5f };

        // Find intersection point (you may need to implement this)
        Vec3f hitPoint = getIntersectionPoint(rayOrigin, rayDir, blockMin, blockMax);

        // Determine which face was hit based on which coordinate is closest to block
        // boundary
        Vec3f localHit = hitPoint - floatBlockPosition;
        Vec3i normal   = Vec3i { 0, 0, 0 };

        float maxComponent = 0.0f;
        if (abs(localHit[0]) > maxComponent) {
          maxComponent = abs(localHit[0]);
          normal       = Vec3i { localHit[0] > 0 ? 1 : -1, 0, 0 };
        }
        if (abs(localHit[1]) > maxComponent) {
          maxComponent = abs(localHit[1]);
          normal       = Vec3i { 0, localHit[1] > 0 ? 1 : -1, 0 };
        }
        if (abs(localHit[2]) > maxComponent) {
          maxComponent = abs(localHit[2]);
          normal       = Vec3i { 0, 0, localHit[2] > 0 ? 1 : -1 };
        }

        Vec3i position = closestBlock->position + normal;
        addBlockToChunkCollection(chunks, { static_cast<int>(id), position });
      }
      if (actBreak) {
        removeBlockToChunkCollection(chunks, *closestBlock);
      }
    }

    sController.getWindow(windowId).clearZBuffer();
    /// TODO: UI first to gain pixel draws
    /// UI Camera

    viewMatrix = BuildViewMatrix(Vec3f { 0, 0, 0 }, Vec3f { 0, 0, 0 });
    vpMatrix   = MatMul(projectionMatrix, viewMatrix);

    modelMatrix = identity<float, 4, 4>();
    Scale(modelMatrix, 0.1f, 0.1f, 1.0f);
    modelMatrix[0][3]     = 0;
    modelMatrix[1][3]     = 0;
    modelMatrix[2][3]     = -0.001;
    Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
    for (size_t k = 0; k < 2; k++) {
      std::array<Vertex, 3> vertexData;
      vertexData[0] = cube[k * 3];
      vertexData[1] = cube[k * 3 + 1];
      vertexData[2] = cube[k * 3 + 2];

      drawTriangle(
          sController.getWindow(windowId),
          vertexData,
          transformMatrix,
          [](Vertex v, Vec3f &fragColor) {
            Vec2f uv  = v.uv - Vec2f { 0.5f, 0.5f };
            fragColor = { 1.0f, 1.0f, 1.0f };
            if (uv[0] < 0.5 && uv[0] > -0.5 && uv[1] < 0.05 && uv[1] > -0.05) return true;
            if (uv[0] < 0.05 && uv[0] > -0.05 && uv[1] < 0.5 && uv[1] > -0.5) return true;
            return false;
          }); // base color
    }

    /// Draw toolbar
    modelMatrix       = identity<float, 4, 4>();
    const float SCALE = 0.1f;
    Scale(modelMatrix, SCALE, SCALE, 1.0f);
    modelMatrix[2][3] = -0.001;
    modelMatrix[1][3] = -3.5 * SCALE;
    for (size_t i = 0; i < 10; i++) {
      modelMatrix[0][3]     = (i - 4.5f) * SCALE;
      Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
      for (size_t k = 0; k < 2; k++) {
        std::array<Vertex, 3> vertexData;
        vertexData[0] = cube[k * 3];
        vertexData[1] = cube[k * 3 + 1];
        vertexData[2] = cube[k * 3 + 2];

        bool selected = id == i;
        drawTriangle(
            sController.getWindow(windowId),
            vertexData,
            transformMatrix,
            [selected](Vertex v, Vec3f &fragColor) {
              Vec2f uv         = v.uv;
              const Image &tex = textures[1];
              int texX         = static_cast<int>(v.uv[0] * (tex.width)) % tex.width;
              int texY         = static_cast<int>(v.uv[1] * (tex.height)) % tex.height;
              int texIndex     = (texY * tex.width + texX) * tex.channels;
              fragColor        = {
                tex.data[texIndex] / 255.0f,
                tex.data[texIndex] / 255.0f,
                tex.data[texIndex] / 255.0f,
              };
              fragColor = fragColor * Vec3f { 2.0f, 2.0f, 2.0f };
              if (!selected) fragColor = fragColor * 0.45f;
              if (uv[0] <= 0.90 && uv[1] <= 0.90 && uv[0] >= 0.1 && uv[1] >= 0.1) {
                fragColor = fragColor * 0.6f;
              }
              return true;
            }); // base color
      }
    }

    sController.getWindow(windowId).clearZBuffer();
    /// Draw toolbar items
    modelMatrix = identity<float, 4, 4>();
    RotateX(modelMatrix, static_cast<float>(-M_PI_4f));
    RotateY(modelMatrix, static_cast<float>(M_PI_4));
    modelMatrix[2][3] = -8.0;
    // modelMatrix[1][3] = -3.5 * SCALE;
    for (size_t i = 0; i < 10 && i < blockDatas.size(); i++) {
      // modelMatrix[0][3]     = (i - 5.0f) * SCALE;
      Mat4f transformMatrix = MatMul(vpMatrix, modelMatrix);
      for (size_t k = 0; k < cube.size() / 3; k++) {
        std::array<Vertex, 3> vertexData;
        vertexData[0]    = cube[k * 3];
        vertexData[1]    = cube[k * 3 + 1];
        vertexData[2]    = cube[k * 3 + 2];
        const Image &tex = blockDatas[i][k / 2];
        float light      = DotProduct(
            Normalize(vertexData[0].normal), Normalize(Vec3f { 0.0f, -2.0f, -1.0f }));
        drawTriangle(
            sController.getWindow(windowId),
            vertexData,
            transformMatrix,
            [&tex, &light](Vertex v, Vec3f &fragColor) {
              int texX     = static_cast<int>(v.uv[0] * (tex.width)) % tex.width;
              int texY     = static_cast<int>(v.uv[1] * (tex.height)) % tex.height;
              int texIndex = (texY * tex.width + texX) * tex.channels;
              fragColor    = {
                tex.data[texIndex] / 255.0f,
                tex.data[texIndex + 1] / 255.0f,
                tex.data[texIndex + 2] / 255.0f
              };
              float lightPow = 0.5f + light * light;
              fragColor *= lightPow;

              if (fragColor[0] > 1.0f) fragColor[0] = 1.0f;
              if (fragColor[1] > 1.0f) fragColor[1] = 1.0f;
              if (fragColor[2] > 1.0f) fragColor[2] = 1.0f;
              if (fragColor[0] < 0.0f) fragColor[0] = 0.0f;
              if (fragColor[1] < 0.0f) fragColor[1] = 0.0f;
              if (fragColor[2] < 0.0f) fragColor[2] = 0.0f;

              return true;
            },
            { (i - 4.5f) * SCALE * 1.2f, -4.2f * SCALE }); // base color
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
