#include "world/ObstacleSystem.hpp"

#include <cgltf.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace world {

namespace {

// =====================================================================
// Procedural kit meshes
// =====================================================================
//
// Each generator returns (positions, normals, indices). All meshes use
// per-vertex normals so the obstacle shader can do Lambert lighting.

struct Mesh {
  std::vector<float>    positions;
  std::vector<float>    normals;
  std::vector<uint32_t> indices;
};

// Closed cylinder centered along +Y, base at Y=0.
Mesh makeCylinder(float radius, float height, int segments) {
  Mesh m;
  const float twoPi = 6.2831853f;

  // Side ring vertices (top + bottom) with face normals pointing outward.
  for (int i = 0; i < segments; ++i) {
    const float a   = (static_cast<float>(i) / segments) * twoPi;
    const float nx  = std::cos(a);
    const float nz  = std::sin(a);
    const float px  = nx * radius;
    const float pz  = nz * radius;
    // bottom
    m.positions.insert(m.positions.end(), { px, 0.0f,    pz });
    m.normals.insert(m.normals.end(),     { nx, 0.0f,    nz });
    // top
    m.positions.insert(m.positions.end(), { px, height,  pz });
    m.normals.insert(m.normals.end(),     { nx, 0.0f,    nz });
  }
  // Side quads (2 triangles per segment)
  for (int i = 0; i < segments; ++i) {
    const uint32_t b0 = static_cast<uint32_t>(2 * i);
    const uint32_t b1 = static_cast<uint32_t>(2 * ((i + 1) % segments));
    m.indices.insert(m.indices.end(), { b0, b0 + 1, b1 + 1,   b0, b1 + 1, b1 });
  }

  // Top cap — fan around a centre vertex.
  const uint32_t topCenter = static_cast<uint32_t>(m.positions.size() / 3);
  m.positions.insert(m.positions.end(), { 0.0f, height, 0.0f });
  m.normals.insert(m.normals.end(),     { 0.0f, 1.0f,   0.0f });
  for (int i = 0; i < segments; ++i) {
    const float a  = (static_cast<float>(i) / segments) * twoPi;
    m.positions.insert(m.positions.end(), { std::cos(a) * radius, height, std::sin(a) * radius });
    m.normals.insert(m.normals.end(),     { 0.0f, 1.0f, 0.0f });
  }
  for (int i = 0; i < segments; ++i) {
    const uint32_t r0 = topCenter + 1 + static_cast<uint32_t>(i);
    const uint32_t r1 = topCenter + 1 + static_cast<uint32_t>((i + 1) % segments);
    m.indices.insert(m.indices.end(), { topCenter, r0, r1 });
  }

  // (Bottom cap skipped — never visible from a top-down RTS-style camera and
  // saves a handful of triangles per cylinder.)
  return m;
}

// Low-poly UV sphere centered at +Y offset, used as a tree canopy. Returns a
// sphere; caller's per-instance position places it.
Mesh makeUvSphere(float radius, float centerY, int latSegs, int lonSegs) {
  Mesh m;
  const float pi    = 3.14159265f;
  const float twoPi = 6.2831853f;

  for (int lat = 0; lat <= latSegs; ++lat) {
    const float v     = static_cast<float>(lat) / latSegs;
    const float theta = v * pi;
    const float sinT  = std::sin(theta);
    const float cosT  = std::cos(theta);
    for (int lon = 0; lon <= lonSegs; ++lon) {
      const float u      = static_cast<float>(lon) / lonSegs;
      const float phi    = u * twoPi;
      const float sinP   = std::sin(phi);
      const float cosP   = std::cos(phi);
      const float nx     = sinT * cosP;
      const float ny     = cosT;
      const float nz     = sinT * sinP;
      m.positions.insert(m.positions.end(), { nx * radius, centerY + ny * radius, nz * radius });
      m.normals.insert(m.normals.end(),     { nx, ny, nz });
    }
  }
  const int stride = lonSegs + 1;
  for (int lat = 0; lat < latSegs; ++lat) {
    for (int lon = 0; lon < lonSegs; ++lon) {
      const uint32_t a = static_cast<uint32_t>(lat       * stride + lon);
      const uint32_t b = static_cast<uint32_t>(lat       * stride + lon + 1);
      const uint32_t c = static_cast<uint32_t>((lat + 1) * stride + lon);
      const uint32_t d = static_cast<uint32_t>((lat + 1) * stride + lon + 1);
      m.indices.insert(m.indices.end(), { a, c, b,   b, c, d });
    }
  }
  return m;
}

// Axis-aligned box, base sitting on Y=0, half-extents in X and Z.
// Uses 6 quads with face-normals (24 vertices) so each face shades crisply.
Mesh makeBox(float hx, float hy, float hz) {
  Mesh m;

  // 6 faces, each contributing 4 vertices + 6 indices.
  auto pushFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
    const uint32_t base = static_cast<uint32_t>(m.positions.size() / 3);
    for (const auto& p : {a, b, c, d}) {
      m.positions.insert(m.positions.end(), { p.x, p.y, p.z });
      m.normals.insert(m.normals.end(),     { n.x, n.y, n.z });
    }
    m.indices.insert(m.indices.end(), { base, base + 1, base + 2,   base, base + 2, base + 3 });
  };

  const float y0 = 0.0f, y1 = hy * 2.0f;  // base at 0, height = hy*2
  // -X
  pushFace({-hx, y0, -hz}, {-hx, y0,  hz}, {-hx, y1,  hz}, {-hx, y1, -hz}, {-1, 0, 0});
  // +X
  pushFace({ hx, y0,  hz}, { hx, y0, -hz}, { hx, y1, -hz}, { hx, y1,  hz}, { 1, 0, 0});
  // -Z
  pushFace({ hx, y0, -hz}, {-hx, y0, -hz}, {-hx, y1, -hz}, { hx, y1, -hz}, { 0, 0,-1});
  // +Z
  pushFace({-hx, y0,  hz}, { hx, y0,  hz}, { hx, y1,  hz}, {-hx, y1,  hz}, { 0, 0, 1});
  // +Y (top)
  pushFace({-hx, y1,  hz}, { hx, y1,  hz}, { hx, y1, -hz}, {-hx, y1, -hz}, { 0, 1, 0});
  // -Y bottom skipped (under the ground for tile-aligned rocks).
  return m;
}

// =====================================================================
// Tile elevation helper (averages 4 corner heights)
// =====================================================================
float tileCenterY(const std::vector<float>& vh, int W, int H, int tx, int ty) {
  const float SW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float SE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  const float NW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float NE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}

// Deterministic small rotation per tile so adjacent obstacles don't look
// stamped from the same template.
float hashRotation(int tx, int ty) {
  uint32_t h = static_cast<uint32_t>(tx) * 1234u + static_cast<uint32_t>(ty) * 5678u;
  h = (h ^ (h >> 13)) * 1274126177u;
  // Map to [0, 2π).
  return static_cast<float>(h & 0xFFFFu) * (6.2831853f / 65536.0f);
}

struct Instance {
  float x, y, z;
  float rotY;
};
static_assert(sizeof(Instance) == 16, "Instance must be tightly packed");

}  // namespace

// =====================================================================
// ObstacleSystem
// =====================================================================

ObstacleSystem::~ObstacleSystem() {
  destroy();
}

void ObstacleSystem::destroy() {
  for (Kit* k : {&trunk_, &canopy_, &rock_, &fence_,
                 &outlineTrunk_, &outlineCanopy_, &outlineRock_, &outlineFence_,
                 &treeTrunkGltf_, &treeCanopyGltf_,
                 &outlineTreeTrunkGltf_, &outlineTreeCanopyGltf_}) {
    if (k->vao)          glDeleteVertexArrays(1, &k->vao);
    if (k->ebo)          glDeleteBuffers(1, &k->ebo);
    if (k->vboNormals)   glDeleteBuffers(1, &k->vboNormals);
    if (k->vboPositions) glDeleteBuffers(1, &k->vboPositions);
    *k = {};
  }
  if (treeInstanceVbo_)            glDeleteBuffers(1, &treeInstanceVbo_);
  if (rockInstanceVbo_)            glDeleteBuffers(1, &rockInstanceVbo_);
  if (fenceInstanceVbo_)           glDeleteBuffers(1, &fenceInstanceVbo_);
  if (outlineInstanceVbo_)         glDeleteBuffers(1, &outlineInstanceVbo_);
  if (treeGltfInstanceVbo_)        glDeleteBuffers(1, &treeGltfInstanceVbo_);
  if (outlineTreeGltfInstanceVbo_) glDeleteBuffers(1, &outlineTreeGltfInstanceVbo_);
  treeInstanceVbo_ = rockInstanceVbo_ = fenceInstanceVbo_ = outlineInstanceVbo_ = 0;
  treeGltfInstanceVbo_ = outlineTreeGltfInstanceVbo_ = 0;
  treeModelLoaded_ = false;
  treeCount_ = rockCount_ = fenceCount_ = 0;
}

void ObstacleSystem::uploadKitMesh(Kit& kit,
                                   const std::vector<float>&    positions,
                                   const std::vector<float>&    normals,
                                   const std::vector<uint32_t>& indices,
                                   GLuint instanceVbo) {
  glCreateBuffers(1, &kit.vboPositions);
  glCreateBuffers(1, &kit.vboNormals);
  glCreateBuffers(1, &kit.ebo);
  glNamedBufferStorage(kit.vboPositions,
                       static_cast<GLsizeiptr>(positions.size() * sizeof(float)),
                       positions.data(), 0);
  glNamedBufferStorage(kit.vboNormals,
                       static_cast<GLsizeiptr>(normals.size() * sizeof(float)),
                       normals.data(), 0);
  glNamedBufferStorage(kit.ebo,
                       static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                       indices.data(), 0);
  kit.indexCount = static_cast<GLsizei>(indices.size());

  glCreateVertexArrays(1, &kit.vao);
  // Attribute 0 = position (vec3) — per vertex
  glVertexArrayVertexBuffer(kit.vao, 0, kit.vboPositions, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(kit.vao, 0);
  glVertexArrayAttribFormat(kit.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(kit.vao, 0, 0);
  // Attribute 1 = normal (vec3) — per vertex
  glVertexArrayVertexBuffer(kit.vao, 1, kit.vboNormals, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(kit.vao, 1);
  glVertexArrayAttribFormat(kit.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(kit.vao, 1, 1);
  // Attributes 2 & 3 = per-instance (vec3 position + float rotation)
  glVertexArrayVertexBuffer(kit.vao, 2, instanceVbo, 0, sizeof(Instance));
  glVertexArrayBindingDivisor(kit.vao, 2, 1);
  glEnableVertexArrayAttrib(kit.vao, 2);
  glVertexArrayAttribFormat(kit.vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, x));
  glVertexArrayAttribBinding(kit.vao, 2, 2);
  glEnableVertexArrayAttrib(kit.vao, 3);
  glVertexArrayAttribFormat(kit.vao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Instance, rotY));
  glVertexArrayAttribBinding(kit.vao, 3, 2);

  glVertexArrayElementBuffer(kit.vao, kit.ebo);
}

void ObstacleSystem::initGL() {
  destroy();

  // Reserve instance VBOs with enough room for plenty of obstacles. Storage
  // is created with DYNAMIC_STORAGE so we can re-upload contents whenever
  // the map regenerates without recreating the buffer.
  glCreateBuffers(1, &treeInstanceVbo_);
  glCreateBuffers(1, &rockInstanceVbo_);
  glCreateBuffers(1, &fenceInstanceVbo_);
  // 4096-instance upper-bound per kind.
  glNamedBufferStorage(treeInstanceVbo_,  sizeof(Instance) * 4096, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(rockInstanceVbo_,  sizeof(Instance) * 4096, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(fenceInstanceVbo_, sizeof(Instance) * 4096, nullptr, GL_DYNAMIC_STORAGE_BIT);

  // ---- Kit meshes -----------------------------------------------------
  // Tree trunk — slim cylinder, 6 sides, 1.0 unit tall.
  const Mesh trunkMesh  = makeCylinder(0.10f, 1.0f, 6);
  uploadKitMesh(trunk_, trunkMesh.positions, trunkMesh.normals, trunkMesh.indices, treeInstanceVbo_);
  trunk_.color = glm::vec3(0.24f, 0.18f, 0.06f);  // dark earth-brown

  // Canopy — UV sphere centered above the trunk top.
  const Mesh canopyMesh = makeUvSphere(/*radius*/0.45f, /*centerY*/1.15f, /*lat*/4, /*lon*/8);
  uploadKitMesh(canopy_, canopyMesh.positions, canopyMesh.normals, canopyMesh.indices, treeInstanceVbo_);
  canopy_.color = glm::vec3(0.12f, 0.32f, 0.06f);  // forest green

  // Rock — wide low box.
  const Mesh rockMesh   = makeBox(0.28f, 0.18f, 0.24f);
  uploadKitMesh(rock_, rockMesh.positions, rockMesh.normals, rockMesh.indices, rockInstanceVbo_);
  rock_.color = glm::vec3(0.39f, 0.27f, 0.15f);  // medium brown

  // Fence — thin horizontal plank (walkable=false, blocksRanged=false).
  // hx=0.5 (full tile width), hy=0.125 (half-height → total 0.25 units tall),
  // hz=0.05 (very thin in Z so it reads as a plank, not a wall).
  const Mesh fenceMesh  = makeBox(0.48f, 0.125f, 0.05f);
  uploadKitMesh(fence_, fenceMesh.positions, fenceMesh.normals, fenceMesh.indices, fenceInstanceVbo_);
  fence_.color = glm::vec3(0.36f, 0.22f, 0.08f);  // warm wood-brown

  // ---- Outline single-instance resources ----------------------------------
  // A separate instance VBO holding exactly 1 instance, used to draw outline
  // shells for the hovered obstacle. Separate VAOs so they bind to this VBO.
  glCreateBuffers(1, &outlineInstanceVbo_);
  glNamedBufferStorage(outlineInstanceVbo_, sizeof(Instance), nullptr, GL_DYNAMIC_STORAGE_BIT);

  uploadKitMesh(outlineTrunk_,  trunkMesh.positions,  trunkMesh.normals,  trunkMesh.indices,  outlineInstanceVbo_);
  uploadKitMesh(outlineCanopy_, canopyMesh.positions, canopyMesh.normals, canopyMesh.indices, outlineInstanceVbo_);
  uploadKitMesh(outlineRock_,   rockMesh.positions,   rockMesh.normals,   rockMesh.indices,   outlineInstanceVbo_);
  uploadKitMesh(outlineFence_,  fenceMesh.positions,  fenceMesh.normals,  fenceMesh.indices,  outlineInstanceVbo_);
}

void ObstacleSystem::rebuildFromMap(const shared::WorldMapFile& map) {
  std::vector<Instance> trees;
  std::vector<Instance> rocks;
  std::vector<Instance> fences;
  trees.reserve(256);
  rocks.reserve(256);
  fences.reserve(64);

  const int W = map.width;
  const int H = map.height;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return;

  for (int ty = 0; ty < H; ++ty) {
    for (int tx = 0; tx < W; ++tx) {
      const auto& tile = map.tiles[ty][tx];
      if (tile.obstacle == shared::ObstacleType::none) continue;

      const float y = tileCenterY(vh, W, H, tx, ty);

      if (tile.obstacle == shared::ObstacleType::tree) {
        trees.push_back({ static_cast<float>(tx), y,
                          static_cast<float>(ty), hashRotation(tx, ty) });
      } else if (tile.obstacle == shared::ObstacleType::rock) {
        rocks.push_back({ static_cast<float>(tx), y,
                          static_cast<float>(ty), hashRotation(tx, ty) });
      } else if (tile.obstacle == shared::ObstacleType::fence) {
        fences.push_back({ static_cast<float>(tx), y,
                           static_cast<float>(ty), hashRotation(tx, ty) });
      }
    }
  }

  // Clamp to the buffer capacity reserved in initGL.
  if (trees.size()  > 4096) trees.resize(4096);
  if (rocks.size()  > 4096) rocks.resize(4096);
  if (fences.size() > 4096) fences.resize(4096);

  const auto treeBytesz  = static_cast<GLsizeiptr>(trees.size()  * sizeof(Instance));
  const auto fenceBytesz = static_cast<GLsizeiptr>(fences.size() * sizeof(Instance));

  if (treeBytesz > 0)
    glNamedBufferSubData(treeInstanceVbo_, 0, treeBytesz, trees.data());
  // The glTF VAOs read from treeGltfInstanceVbo_; keep it in sync.
  if (treeModelLoaded_ && treeGltfInstanceVbo_ && treeBytesz > 0)
    glNamedBufferSubData(treeGltfInstanceVbo_, 0, treeBytesz, trees.data());
  if (!rocks.empty())
    glNamedBufferSubData(rockInstanceVbo_, 0,
                         static_cast<GLsizeiptr>(rocks.size() * sizeof(Instance)),
                         rocks.data());
  if (fenceBytesz > 0)
    glNamedBufferSubData(fenceInstanceVbo_, 0, fenceBytesz, fences.data());

  treeCount_  = trees.size();
  rockCount_  = rocks.size();
  fenceCount_ = fences.size();

  std::fprintf(stdout, "[ObstacleSystem] %zu trees, %zu rocks, %zu fences\n",
               treeCount_, rockCount_, fenceCount_);
}

void ObstacleSystem::render(render::Shader& obstacleShader) {
  // The caller has already set u_viewProj, u_lightDir, and the palette
  // uniforms. We just bind each VAO, set u_color, and issue an instanced draw.
  if (treeCount_ > 0) {
    if (treeModelLoaded_) {
      if (treeTrunkGltf_.vao) {
        obstacleShader.setVec3("u_color", treeTrunkGltf_.color);
        glBindVertexArray(treeTrunkGltf_.vao);
        glDrawElementsInstanced(GL_TRIANGLES, treeTrunkGltf_.indexCount,
                                GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(treeCount_));
      }
      if (treeCanopyGltf_.vao) {
        obstacleShader.setVec3("u_color", treeCanopyGltf_.color);
        glBindVertexArray(treeCanopyGltf_.vao);
        glDrawElementsInstanced(GL_TRIANGLES, treeCanopyGltf_.indexCount,
                                GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(treeCount_));
      }
    } else {
      obstacleShader.setVec3("u_color", trunk_.color);
      glBindVertexArray(trunk_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, trunk_.indexCount, GL_UNSIGNED_INT,
                              nullptr, static_cast<GLsizei>(treeCount_));
      obstacleShader.setVec3("u_color", canopy_.color);
      glBindVertexArray(canopy_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, canopy_.indexCount, GL_UNSIGNED_INT,
                              nullptr, static_cast<GLsizei>(treeCount_));
    }
  }
  if (rockCount_ > 0) {
    obstacleShader.setVec3("u_color", rock_.color);
    glBindVertexArray(rock_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, rock_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(rockCount_));
  }
  if (fenceCount_ > 0) {
    obstacleShader.setVec3("u_color", fence_.color);
    glBindVertexArray(fence_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, fence_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(fenceCount_));
  }
  glBindVertexArray(0);
}

void ObstacleSystem::renderDepth(render::Shader& /*depthShader*/) {
  if (treeCount_ > 0) {
    if (treeModelLoaded_) {
      if (treeTrunkGltf_.vao) {
        glBindVertexArray(treeTrunkGltf_.vao);
        glDrawElementsInstanced(GL_TRIANGLES, treeTrunkGltf_.indexCount,
                                GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(treeCount_));
      }
      if (treeCanopyGltf_.vao) {
        glBindVertexArray(treeCanopyGltf_.vao);
        glDrawElementsInstanced(GL_TRIANGLES, treeCanopyGltf_.indexCount,
                                GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(treeCount_));
      }
    } else {
      glBindVertexArray(trunk_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, trunk_.indexCount, GL_UNSIGNED_INT,
                              nullptr, static_cast<GLsizei>(treeCount_));
      glBindVertexArray(canopy_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, canopy_.indexCount, GL_UNSIGNED_INT,
                              nullptr, static_cast<GLsizei>(treeCount_));
    }
  }
  if (rockCount_ > 0) {
    glBindVertexArray(rock_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, rock_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(rockCount_));
  }
  if (fenceCount_ > 0) {
    glBindVertexArray(fence_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, fence_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(fenceCount_));
  }
  glBindVertexArray(0);
}

bool ObstacleSystem::renderOutlineAt(render::Shader& outlineShader,
                                     const shared::WorldMapFile& map,
                                     int tileX, int tileY) {
  if (tileY < 0 || tileY >= map.height || tileX < 0 || tileX >= map.width) return false;
  const auto obs = map.tiles[tileY][tileX].obstacle;
  if (obs == shared::ObstacleType::none ||
      obs == shared::ObstacleType::fishing_spot) return false;

  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (map.width + 1) * (map.height + 1)) return false;

  const float cy = tileCenterY(vh, map.width, map.height, tileX, tileY);
  const bool  isTree  = (obs == shared::ObstacleType::tree);
  const bool  isFence = (obs == shared::ObstacleType::fence);

  // Upload the single-instance data into the appropriate outline VBO.
  // Instance position must match what rebuildFromMap uploaded:
  // trees and rocks are both centred at (tileX, cy, tileY).
  const Instance inst{ static_cast<float>(tileX), cy,
                       static_cast<float>(tileY),
                       hashRotation(tileX, tileY) };
  if (isTree && treeModelLoaded_) {
    glNamedBufferSubData(outlineTreeGltfInstanceVbo_, 0, sizeof(Instance), &inst);
  } else {
    glNamedBufferSubData(outlineInstanceVbo_, 0, sizeof(Instance), &inst);
  }

  // Draw the appropriate kit(s) for this obstacle type. All kits share the
  // same VAO layout and the same instance buffer updated above.
  auto drawKits = [&]() {
    if (isTree) {
      if (treeModelLoaded_) {
        if (outlineTreeTrunkGltf_.vao) {
          glBindVertexArray(outlineTreeTrunkGltf_.vao);
          glDrawElementsInstanced(GL_TRIANGLES, outlineTreeTrunkGltf_.indexCount,
                                  GL_UNSIGNED_INT, nullptr, 1);
        }
        if (outlineTreeCanopyGltf_.vao) {
          glBindVertexArray(outlineTreeCanopyGltf_.vao);
          glDrawElementsInstanced(GL_TRIANGLES, outlineTreeCanopyGltf_.indexCount,
                                  GL_UNSIGNED_INT, nullptr, 1);
        }
      } else {
        glBindVertexArray(outlineTrunk_.vao);
        glDrawElementsInstanced(GL_TRIANGLES, outlineTrunk_.indexCount,
                                GL_UNSIGNED_INT, nullptr, 1);
        glBindVertexArray(outlineCanopy_.vao);
        glDrawElementsInstanced(GL_TRIANGLES, outlineCanopy_.indexCount,
                                GL_UNSIGNED_INT, nullptr, 1);
      }
    } else if (isFence) {
      glBindVertexArray(outlineFence_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, outlineFence_.indexCount,
                              GL_UNSIGNED_INT, nullptr, 1);
    } else {
      // rock or chest — both use the rock Kit shape
      glBindVertexArray(outlineRock_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, outlineRock_.indexCount,
                              GL_UNSIGNED_INT, nullptr, 1);
    }
    glBindVertexArray(0);
  };

  // ── Pass 1: Stencil write ─────────────────────────────────────────────────
  // Render the unmodified geometry into the stencil buffer only. Every
  // fragment that belongs to the visible obstacle silhouette gets stencil=1.
  // Colour and depth writes are suppressed; depth test uses LEQUAL so the
  // already-drawn obstacle pixels (equal depth) pass.
  glEnable(GL_STENCIL_TEST);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glStencilMask(0xFF);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);  // write 1 on depth pass

  outlineShader.setFloat("u_outlineWidth", 0.0f);  // no inflation — stamp exact silhouette
  drawKits();

  // ── Pass 2: Inflated outline, silhouette-only ─────────────────────────────
  // Render the geometry inflated outward along normals, but accept only the
  // ring of pixels outside the stencil-marked interior (NOTEQUAL 1). Culling
  // is disabled — the stencil clips the interior; no cull-face trick needed.
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
  glStencilMask(0x00);          // don't modify stencil during this pass
  glDisable(GL_CULL_FACE);

  outlineShader.setFloat("u_outlineWidth", 0.06f);
  drawKits();

  // ── Restore GL state ──────────────────────────────────────────────────────
  glEnable(GL_CULL_FACE);       // back-face culling back on (cull mode unchanged = GL_BACK)
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glStencilMask(0xFF);
  glDisable(GL_STENCIL_TEST);
  return true;
}

bool ObstacleSystem::loadTreeModel(const std::filesystem::path& path) {
  cgltf_options opts{};
  cgltf_data*   data = nullptr;
  const std::string pathStr = path.string();

  if (cgltf_parse_file(&opts, pathStr.c_str(), &data) != cgltf_result_success) {
    std::fprintf(stderr, "[ObstacleSystem] tree model parse failed: %s\n", pathStr.c_str());
    return false;
  }
  if (cgltf_load_buffers(&opts, data, pathStr.c_str()) != cgltf_result_success) {
    std::fprintf(stderr, "[ObstacleSystem] tree model load_buffers failed\n");
    cgltf_free(data);
    return false;
  }

  Mesh trunkMesh, canopyMesh;

  // Scale model to a 1×1 tile footprint (original X/Z width ≈ 0.94 tiles)
  // and roughly 2× player height in Y.  Separate XZ / Y scales let us
  // control the silhouette independently of the height.
  constexpr float kScaleXZ = 1.066f;  // 1.0 / 0.938 ≈ fills exactly 1 tile wide
  constexpr float kScaleY  = 3.5f;    // ≈ 2× player height (~1.75 units tall)

  for (size_t ni = 0; ni < data->nodes_count; ++ni) {
    const cgltf_node* node = &data->nodes[ni];
    if (!node->mesh) continue;

    // Get the world transform for this node (accounts for all parent transforms).
    float mat[16];
    cgltf_node_transform_world(node, mat);

    for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
      const cgltf_primitive* prim = &node->mesh->primitives[pi];

      const cgltf_accessor* posAcc = nullptr;
      const cgltf_accessor* nrmAcc = nullptr;
      for (size_t ai = 0; ai < prim->attributes_count; ++ai) {
        if (prim->attributes[ai].type == cgltf_attribute_type_position)
          posAcc = prim->attributes[ai].data;
        else if (prim->attributes[ai].type == cgltf_attribute_type_normal)
          nrmAcc = prim->attributes[ai].data;
      }
      if (!posAcc) continue;

      // Material index 0 → trunk, 1+ → canopy.
      int matIdx = -1;
      if (prim->material) {
        for (size_t mi = 0; mi < data->materials_count; ++mi) {
          if (&data->materials[mi] == prim->material) { matIdx = static_cast<int>(mi); break; }
        }
      }
      Mesh& target = (matIdx == 0) ? trunkMesh : canopyMesh;
      const uint32_t baseVert = static_cast<uint32_t>(target.positions.size() / 3);

      // Positions — apply node world transform then scale.
      for (size_t vi = 0; vi < posAcc->count; ++vi) {
        float p[3] = {};
        cgltf_accessor_read_float(posAcc, vi, p, 3);
        // Column-major 4×4 matrix multiply (translation in last column).
        const float lx = mat[0]*p[0] + mat[4]*p[1] + mat[8]*p[2]  + mat[12];
        const float ly = mat[1]*p[0] + mat[5]*p[1] + mat[9]*p[2]  + mat[13];
        const float lz = mat[2]*p[0] + mat[6]*p[1] + mat[10]*p[2] + mat[14];
        target.positions.insert(target.positions.end(),
                                {lx * kScaleXZ, ly * kScaleY, lz * kScaleXZ});
      }

      // Normals — apply rotation part of the matrix only.
      if (nrmAcc) {
        for (size_t vi = 0; vi < nrmAcc->count; ++vi) {
          float n[3] = {};
          cgltf_accessor_read_float(nrmAcc, vi, n, 3);
          float nx = mat[0]*n[0] + mat[4]*n[1] + mat[8]*n[2];
          float ny = mat[1]*n[0] + mat[5]*n[1] + mat[9]*n[2];
          float nz = mat[2]*n[0] + mat[6]*n[1] + mat[10]*n[2];
          const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
          if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
          target.normals.insert(target.normals.end(), {nx, ny, nz});
        }
      } else {
        for (size_t vi = 0; vi < posAcc->count; ++vi)
          target.normals.insert(target.normals.end(), {0.0f, 1.0f, 0.0f});
      }

      // Indices.
      if (prim->indices) {
        for (size_t ii = 0; ii < prim->indices->count; ++ii) {
          uint32_t idx = 0;
          cgltf_accessor_read_uint(prim->indices, ii, &idx, 1);
          target.indices.push_back(baseVert + idx);
        }
      } else {
        for (uint32_t vi = 0; vi < static_cast<uint32_t>(posAcc->count); ++vi)
          target.indices.push_back(baseVert + vi);
      }
    }
  }
  cgltf_free(data);

  if (trunkMesh.positions.empty() && canopyMesh.positions.empty()) {
    std::fprintf(stderr, "[ObstacleSystem] tree model: no geometry found\n");
    return false;
  }

  // Create instance VBOs (capacity for 4096 instances each).
  if (!treeGltfInstanceVbo_) {
    glCreateBuffers(1, &treeGltfInstanceVbo_);
    glNamedBufferStorage(treeGltfInstanceVbo_, sizeof(Instance) * 4096,
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
  }
  if (!outlineTreeGltfInstanceVbo_) {
    glCreateBuffers(1, &outlineTreeGltfInstanceVbo_);
    glNamedBufferStorage(outlineTreeGltfInstanceVbo_, sizeof(Instance),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
  }

  if (!trunkMesh.positions.empty()) {
    uploadKitMesh(treeTrunkGltf_, trunkMesh.positions, trunkMesh.normals,
                  trunkMesh.indices, treeGltfInstanceVbo_);
    treeTrunkGltf_.color = glm::vec3(0.24f, 0.18f, 0.06f);
    uploadKitMesh(outlineTreeTrunkGltf_, trunkMesh.positions, trunkMesh.normals,
                  trunkMesh.indices, outlineTreeGltfInstanceVbo_);
  }
  if (!canopyMesh.positions.empty()) {
    uploadKitMesh(treeCanopyGltf_, canopyMesh.positions, canopyMesh.normals,
                  canopyMesh.indices, treeGltfInstanceVbo_);
    treeCanopyGltf_.color = glm::vec3(0.12f, 0.32f, 0.06f);
    uploadKitMesh(outlineTreeCanopyGltf_, canopyMesh.positions, canopyMesh.normals,
                  canopyMesh.indices, outlineTreeGltfInstanceVbo_);
  }

  // Compute model-space AABB from all scaled vertex positions (used by the
  // pick loop to do geometry-accurate hover detection).
  {
    glm::vec3 bMin( 1e9f,  1e9f,  1e9f);
    glm::vec3 bMax(-1e9f, -1e9f, -1e9f);
    auto accumMesh = [&](const Mesh& m) {
      for (std::size_t i = 0; i + 2 < m.positions.size(); i += 3) {
        bMin.x = std::min(bMin.x, m.positions[i]);
        bMin.y = std::min(bMin.y, m.positions[i + 1]);
        bMin.z = std::min(bMin.z, m.positions[i + 2]);
        bMax.x = std::max(bMax.x, m.positions[i]);
        bMax.y = std::max(bMax.y, m.positions[i + 1]);
        bMax.z = std::max(bMax.z, m.positions[i + 2]);
      }
    };
    accumMesh(trunkMesh);
    accumMesh(canopyMesh);
    if (bMin.x < bMax.x) {
      treeGltfAABBMin_ = bMin;
      treeGltfAABBMax_ = bMax;
    }
  }

  treeModelLoaded_ = true;
  std::fprintf(stdout, "[ObstacleSystem] tree.gltf loaded — %zu trunk verts, %zu canopy verts  "
               "AABB [%.2f,%.2f,%.2f]..[%.2f,%.2f,%.2f]\n",
               trunkMesh.positions.size() / 3, canopyMesh.positions.size() / 3,
               treeGltfAABBMin_.x, treeGltfAABBMin_.y, treeGltfAABBMin_.z,
               treeGltfAABBMax_.x, treeGltfAABBMax_.y, treeGltfAABBMax_.z);
  return true;
}

}  // namespace world
