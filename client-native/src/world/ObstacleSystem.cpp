#include "world/ObstacleSystem.hpp"

#include <cmath>
#include <cstdio>

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
  for (Kit* k : {&trunk_, &canopy_, &rock_,
                 &outlineTrunk_, &outlineCanopy_, &outlineRock_}) {
    if (k->vao)          glDeleteVertexArrays(1, &k->vao);
    if (k->ebo)          glDeleteBuffers(1, &k->ebo);
    if (k->vboNormals)   glDeleteBuffers(1, &k->vboNormals);
    if (k->vboPositions) glDeleteBuffers(1, &k->vboPositions);
    *k = {};
  }
  if (treeInstanceVbo_) glDeleteBuffers(1, &treeInstanceVbo_);
  if (rockInstanceVbo_) glDeleteBuffers(1, &rockInstanceVbo_);
  if (outlineInstanceVbo_) glDeleteBuffers(1, &outlineInstanceVbo_);
  treeInstanceVbo_ = rockInstanceVbo_ = outlineInstanceVbo_ = 0;
  treeCount_ = rockCount_ = 0;
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
  // 4 KiB upper-bound = 256 instances per kind. Map regenerates if exceeded.
  glNamedBufferStorage(treeInstanceVbo_, sizeof(Instance) * 4096, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(rockInstanceVbo_, sizeof(Instance) * 4096, nullptr, GL_DYNAMIC_STORAGE_BIT);

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

  // ---- Outline single-instance resources ----------------------------------
  // A separate instance VBO holding exactly 1 instance, used to draw outline
  // shells for the hovered obstacle. Separate VAOs so they bind to this VBO.
  glCreateBuffers(1, &outlineInstanceVbo_);
  glNamedBufferStorage(outlineInstanceVbo_, sizeof(Instance), nullptr, GL_DYNAMIC_STORAGE_BIT);

  uploadKitMesh(outlineTrunk_,  trunkMesh.positions,  trunkMesh.normals,  trunkMesh.indices,  outlineInstanceVbo_);
  uploadKitMesh(outlineCanopy_, canopyMesh.positions, canopyMesh.normals, canopyMesh.indices, outlineInstanceVbo_);
  uploadKitMesh(outlineRock_,   rockMesh.positions,   rockMesh.normals,   rockMesh.indices,   outlineInstanceVbo_);
}

void ObstacleSystem::rebuildFromMap(const shared::WorldMapFile& map) {
  std::vector<Instance> trees;
  std::vector<Instance> rocks;
  trees.reserve(256);
  rocks.reserve(256);

  const int W = map.width;
  const int H = map.height;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return;

  for (int ty = 0; ty < H; ++ty) {
    for (int tx = 0; tx < W; ++tx) {
      const auto& tile = map.tiles[ty][tx];
      if (tile.obstacle == shared::ObstacleType::none) continue;

      const float y = tileCenterY(vh, W, H, tx, ty);
      Instance inst{ static_cast<float>(tx), y, static_cast<float>(ty),
                     hashRotation(tx, ty) };

      if (tile.obstacle == shared::ObstacleType::tree)      trees.push_back(inst);
      else if (tile.obstacle == shared::ObstacleType::rock) rocks.push_back(inst);
    }
  }

  // Clamp to the buffer capacity reserved in initGL.
  if (trees.size() > 4096) trees.resize(4096);
  if (rocks.size() > 4096) rocks.resize(4096);

  glNamedBufferSubData(treeInstanceVbo_, 0,
                       static_cast<GLsizeiptr>(trees.size() * sizeof(Instance)),
                       trees.data());
  glNamedBufferSubData(rockInstanceVbo_, 0,
                       static_cast<GLsizeiptr>(rocks.size() * sizeof(Instance)),
                       rocks.data());

  treeCount_ = trees.size();
  rockCount_ = rocks.size();

  std::fprintf(stdout, "[ObstacleSystem] %zu trees, %zu rocks\n", treeCount_, rockCount_);
}

void ObstacleSystem::render(render::Shader& obstacleShader) {
  // The caller has already set u_viewProj, u_lightDir, and the palette
  // uniforms. We just bind each VAO, set u_color, and issue an instanced
  // draw.
  if (treeCount_ > 0) {
    obstacleShader.setVec3("u_color", trunk_.color);
    glBindVertexArray(trunk_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, trunk_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(treeCount_));

    obstacleShader.setVec3("u_color", canopy_.color);
    glBindVertexArray(canopy_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, canopy_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(treeCount_));
  }
  if (rockCount_ > 0) {
    obstacleShader.setVec3("u_color", rock_.color);
    glBindVertexArray(rock_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, rock_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(rockCount_));
  }
  glBindVertexArray(0);
}

void ObstacleSystem::renderDepth(render::Shader& /*depthShader*/) {
  // Same VAOs as the regular render path. The bound program ignores normal
  // + color attributes; it only reads position + per-instance pos/rotY.
  if (treeCount_ > 0) {
    glBindVertexArray(trunk_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, trunk_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(treeCount_));
    glBindVertexArray(canopy_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, canopy_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(treeCount_));
  }
  if (rockCount_ > 0) {
    glBindVertexArray(rock_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, rock_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(rockCount_));
  }
  glBindVertexArray(0);
}

bool ObstacleSystem::renderOutlineAt(render::Shader& /*outlineShader*/,
                                     const shared::WorldMapFile& map,
                                     int tileX, int tileY) {
  if (tileY < 0 || tileY >= map.height || tileX < 0 || tileX >= map.width) return false;
  const auto obs = map.tiles[tileY][tileX].obstacle;
  if (obs != shared::ObstacleType::tree && obs != shared::ObstacleType::rock) return false;

  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (map.width + 1) * (map.height + 1)) return false;

  const float cy = tileCenterY(vh, map.width, map.height, tileX, tileY);
  Instance inst{ static_cast<float>(tileX), cy, static_cast<float>(tileY),
                 hashRotation(tileX, tileY) };
  glNamedBufferSubData(outlineInstanceVbo_, 0, sizeof(Instance), &inst);

  // In our left-handed projection (lookAtLH/perspectiveLH) the screen-space
  // winding is inverted. Tell GL that CW = front so face classification
  // matches visual reality, then cull front faces to show only the back
  // shell (the outline rim extending beyond the original silhouette).
  glFrontFace(GL_CW);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);
  glDepthMask(GL_FALSE);  // don't write depth — outline is visual-only

  if (obs == shared::ObstacleType::tree) {
    glBindVertexArray(outlineTrunk_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, outlineTrunk_.indexCount,
                            GL_UNSIGNED_INT, nullptr, 1);
    glBindVertexArray(outlineCanopy_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, outlineCanopy_.indexCount,
                            GL_UNSIGNED_INT, nullptr, 1);
  } else {
    glBindVertexArray(outlineRock_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, outlineRock_.indexCount,
                            GL_UNSIGNED_INT, nullptr, 1);
  }

  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glFrontFace(GL_CCW);  // restore default
  glBindVertexArray(0);
  return true;
}

}  // namespace world
