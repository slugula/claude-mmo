#include "world/EntityRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace world {

namespace {

// =====================================================================
// Procedural geometry helpers (mirroring ObstacleSystem.cpp's style)
// =====================================================================

struct Mesh {
  std::vector<float>    positions;
  std::vector<float>    normals;
  std::vector<uint32_t> indices;
};

// Closed cylinder centered along +Y with base at Y=0, top cap, no bottom.
void appendCylinder(Mesh& m, float radius, float height, int segments,
                    float yOffset = 0.0f) {
  const float twoPi = 6.2831853f;
  const uint32_t baseV = static_cast<uint32_t>(m.positions.size() / 3);

  for (int i = 0; i < segments; ++i) {
    const float a  = (static_cast<float>(i) / segments) * twoPi;
    const float nx = std::cos(a);
    const float nz = std::sin(a);
    m.positions.insert(m.positions.end(), { nx * radius, yOffset,          nz * radius });
    m.normals.insert(m.normals.end(),     { nx, 0.0f, nz });
    m.positions.insert(m.positions.end(), { nx * radius, yOffset + height, nz * radius });
    m.normals.insert(m.normals.end(),     { nx, 0.0f, nz });
  }
  for (int i = 0; i < segments; ++i) {
    const uint32_t b0 = baseV + static_cast<uint32_t>(2 * i);
    const uint32_t b1 = baseV + static_cast<uint32_t>(2 * ((i + 1) % segments));
    m.indices.insert(m.indices.end(), { b0, b0 + 1, b1 + 1,   b0, b1 + 1, b1 });
  }

  // Top cap
  const uint32_t topCenter = static_cast<uint32_t>(m.positions.size() / 3);
  m.positions.insert(m.positions.end(), { 0.0f, yOffset + height, 0.0f });
  m.normals.insert(m.normals.end(),     { 0.0f, 1.0f, 0.0f });
  for (int i = 0; i < segments; ++i) {
    const float a = (static_cast<float>(i) / segments) * twoPi;
    m.positions.insert(m.positions.end(), { std::cos(a) * radius, yOffset + height, std::sin(a) * radius });
    m.normals.insert(m.normals.end(),     { 0.0f, 1.0f, 0.0f });
  }
  for (int i = 0; i < segments; ++i) {
    const uint32_t r0 = topCenter + 1 + static_cast<uint32_t>(i);
    const uint32_t r1 = topCenter + 1 + static_cast<uint32_t>((i + 1) % segments);
    m.indices.insert(m.indices.end(), { topCenter, r0, r1 });
  }
}

// Low-poly UV sphere at a specified center.
void appendSphere(Mesh& m, float radius, float cx, float cy, float cz,
                  int latSegs, int lonSegs) {
  const float pi    = 3.14159265f;
  const float twoPi = 6.2831853f;
  const uint32_t baseV = static_cast<uint32_t>(m.positions.size() / 3);

  for (int lat = 0; lat <= latSegs; ++lat) {
    const float v     = static_cast<float>(lat) / latSegs;
    const float theta = v * pi;
    const float sinT  = std::sin(theta);
    const float cosT  = std::cos(theta);
    for (int lon = 0; lon <= lonSegs; ++lon) {
      const float u    = static_cast<float>(lon) / lonSegs;
      const float phi  = u * twoPi;
      const float sinP = std::sin(phi);
      const float cosP = std::cos(phi);
      const float nx   = sinT * cosP;
      const float ny   = cosT;
      const float nz   = sinT * sinP;
      m.positions.insert(m.positions.end(), { cx + nx * radius, cy + ny * radius, cz + nz * radius });
      m.normals.insert(m.normals.end(),     { nx, ny, nz });
    }
  }
  const int stride = lonSegs + 1;
  for (int lat = 0; lat < latSegs; ++lat) {
    for (int lon = 0; lon < lonSegs; ++lon) {
      const uint32_t a = baseV + static_cast<uint32_t>(lat       * stride + lon);
      const uint32_t b = baseV + static_cast<uint32_t>(lat       * stride + lon + 1);
      const uint32_t c = baseV + static_cast<uint32_t>((lat + 1) * stride + lon);
      const uint32_t d = baseV + static_cast<uint32_t>((lat + 1) * stride + lon + 1);
      m.indices.insert(m.indices.end(), { a, c, b,   b, c, d });
    }
  }
}

// Axis-aligned box, base at Y=0, faces with normals (no bottom face).
void appendBox(Mesh& m, float hx, float hy, float hz, float yOffset = 0.0f) {
  auto pushFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
    const uint32_t base = static_cast<uint32_t>(m.positions.size() / 3);
    for (const auto& p : {a, b, c, d}) {
      m.positions.insert(m.positions.end(), { p.x, p.y, p.z });
      m.normals.insert(m.normals.end(),     { n.x, n.y, n.z });
    }
    m.indices.insert(m.indices.end(), { base, base + 1, base + 2,   base, base + 2, base + 3 });
  };

  const float y0 = yOffset, y1 = yOffset + hy * 2.0f;
  pushFace({-hx, y0, -hz}, {-hx, y0,  hz}, {-hx, y1,  hz}, {-hx, y1, -hz}, {-1, 0, 0});
  pushFace({ hx, y0,  hz}, { hx, y0, -hz}, { hx, y1, -hz}, { hx, y1,  hz}, { 1, 0, 0});
  pushFace({ hx, y0, -hz}, {-hx, y0, -hz}, {-hx, y1, -hz}, { hx, y1, -hz}, { 0, 0,-1});
  pushFace({-hx, y0,  hz}, { hx, y0,  hz}, { hx, y1,  hz}, {-hx, y1,  hz}, { 0, 0, 1});
  pushFace({-hx, y1,  hz}, { hx, y1,  hz}, { hx, y1, -hz}, {-hx, y1, -hz}, { 0, 1, 0});
}

float tileWorldY(const shared::WorldMapFile& map, int tx, int ty) {
  const int W = map.width;
  const int H = map.height;
  if (W <= 0 || H <= 0 || tx < 0 || ty < 0 || tx >= W || ty >= H) return 0.0f;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return 0.0f;
  const float SW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float SE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  const float NW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float NE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}

// Server's facing string -> Y-axis yaw in radians.
// Matches App.cpp's facingToYaw exactly; duplicated here so the entity
// renderer doesn't pull in App-level headers.
float facingToYaw(const std::string& facing) {
  if (facing == "north") return 3.14159265f;
  if (facing == "east")  return  1.57079632f;
  if (facing == "west")  return -1.57079632f;
  return 0.0f;  // "south" or unknown -> rest
}

using Instance = EntityRenderer::Instance;
static_assert(sizeof(Instance) == 16, "Instance must be tightly packed");

}  // namespace

// =====================================================================
// EntityRenderer
// =====================================================================

EntityRenderer::~EntityRenderer() {
  destroy();
}

void EntityRenderer::destroy() {
  for (Kit* k : {&humanoid_, &itemBox_}) {
    if (k->vao)    glDeleteVertexArrays(1, &k->vao);
    if (k->ebo)    glDeleteBuffers(1, &k->ebo);
    if (k->vboNrm) glDeleteBuffers(1, &k->vboNrm);
    if (k->vboPos) glDeleteBuffers(1, &k->vboPos);
    *k = {};
  }
  if (npcInstanceVbo_)  glDeleteBuffers(1, &npcInstanceVbo_);
  if (itemInstanceVbo_) glDeleteBuffers(1, &itemInstanceVbo_);
  npcInstanceVbo_ = itemInstanceVbo_ = 0;
  npcCount_ = itemCount_ = 0;
}

void EntityRenderer::uploadKit(Kit& kit,
                               const std::vector<float>&    positions,
                               const std::vector<float>&    normals,
                               const std::vector<uint32_t>& indices,
                               GLuint instanceVbo) {
  glCreateBuffers(1, &kit.vboPos);
  glCreateBuffers(1, &kit.vboNrm);
  glCreateBuffers(1, &kit.ebo);
  glNamedBufferStorage(kit.vboPos,
                       static_cast<GLsizeiptr>(positions.size() * sizeof(float)),
                       positions.data(), 0);
  glNamedBufferStorage(kit.vboNrm,
                       static_cast<GLsizeiptr>(normals.size() * sizeof(float)),
                       normals.data(), 0);
  glNamedBufferStorage(kit.ebo,
                       static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                       indices.data(), 0);
  kit.indexCount = static_cast<GLsizei>(indices.size());

  glCreateVertexArrays(1, &kit.vao);
  // 0 = position
  glVertexArrayVertexBuffer(kit.vao, 0, kit.vboPos, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(kit.vao, 0);
  glVertexArrayAttribFormat(kit.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(kit.vao, 0, 0);
  // 1 = normal
  glVertexArrayVertexBuffer(kit.vao, 1, kit.vboNrm, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(kit.vao, 1);
  glVertexArrayAttribFormat(kit.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(kit.vao, 1, 1);
  // 2 / 3 = per-instance position + rotation (matches obstacle shader)
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

void EntityRenderer::initGL() {
  destroy();

  glCreateBuffers(1, &npcInstanceVbo_);
  glCreateBuffers(1, &itemInstanceVbo_);
  glNamedBufferStorage(npcInstanceVbo_,  sizeof(Instance) * kInstanceCap, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(itemInstanceVbo_, sizeof(Instance) * kInstanceCap, nullptr, GL_DYNAMIC_STORAGE_BIT);

  // Humanoid for NPCs: cylinder body + sphere head
  Mesh humanoid;
  appendCylinder(humanoid, /*radius*/0.18f, /*height*/0.70f, /*segs*/6, /*yOff*/0.0f);
  appendSphere  (humanoid, /*radius*/0.15f, 0.0f, 0.85f, 0.0f, /*lat*/4, /*lon*/8);
  uploadKit(humanoid_, humanoid.positions, humanoid.normals, humanoid.indices, npcInstanceVbo_);
  humanoid_.color = glm::vec3(0.51f, 0.49f, 0.20f);  // muted olive — palette-adjacent

  // Box for dropped items
  Mesh box;
  appendBox(box, /*hx*/0.15f, /*hy*/0.12f, /*hz*/0.15f, /*yOff*/0.05f);
  uploadKit(itemBox_, box.positions, box.normals, box.indices, itemInstanceVbo_);
  itemBox_.color = glm::vec3(0.66f, 0.58f, 0.24f);  // warm gold
}

void EntityRenderer::rebuildNpcs(const std::vector<shared::NPCState>& npcs,
                                 const shared::WorldMapFile&          map) {
  std::vector<Instance> insts;
  insts.reserve(npcs.size());
  for (const auto& npc : npcs) {
    if (npc.dying) continue;  // hide the dying ones for now (Phase 5d MVP)
    if (npc.tileX < 0 || npc.tileY < 0
        || npc.tileX >= map.width || npc.tileY >= map.height) continue;
    insts.push_back({
        static_cast<float>(npc.tileX),
        tileWorldY(map, npc.tileX, npc.tileY),
        static_cast<float>(npc.tileY),
        facingToYaw(npc.facing),
    });
  }
  if (insts.size() > kInstanceCap) insts.resize(kInstanceCap);
  glNamedBufferSubData(npcInstanceVbo_, 0,
                       static_cast<GLsizeiptr>(insts.size() * sizeof(Instance)),
                       insts.data());
  npcCount_ = insts.size();
}

void EntityRenderer::rebuildItems(const std::vector<shared::DroppedItemState>& items,
                                  const shared::WorldMapFile&                  map) {
  std::vector<Instance> insts;
  insts.reserve(items.size());
  for (const auto& it : items) {
    if (it.tileX < 0 || it.tileY < 0
        || it.tileX >= map.width || it.tileY >= map.height) continue;
    insts.push_back({
        static_cast<float>(it.tileX),
        tileWorldY(map, it.tileX, it.tileY),
        static_cast<float>(it.tileY),
        0.0f,
    });
  }
  if (insts.size() > kInstanceCap) insts.resize(kInstanceCap);
  glNamedBufferSubData(itemInstanceVbo_, 0,
                       static_cast<GLsizeiptr>(insts.size() * sizeof(Instance)),
                       insts.data());
  itemCount_ = insts.size();
}

void EntityRenderer::setNpcInstances(const std::vector<Instance>& insts) {
  const std::size_t n = std::min(insts.size(), kInstanceCap);
  if (n > 0) {
    glNamedBufferSubData(npcInstanceVbo_, 0,
                         static_cast<GLsizeiptr>(n * sizeof(Instance)),
                         insts.data());
  }
  npcCount_ = n;
}

void EntityRenderer::setItemInstances(const std::vector<Instance>& insts) {
  const std::size_t n = std::min(insts.size(), kInstanceCap);
  if (n > 0) {
    glNamedBufferSubData(itemInstanceVbo_, 0,
                         static_cast<GLsizeiptr>(n * sizeof(Instance)),
                         insts.data());
  }
  itemCount_ = n;
}

void EntityRenderer::render(render::Shader& shader) {
  if (npcCount_ > 0) {
    shader.setVec3("u_color", humanoid_.color);
    glBindVertexArray(humanoid_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, humanoid_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(npcCount_));
  }
  if (itemCount_ > 0) {
    shader.setVec3("u_color", itemBox_.color);
    glBindVertexArray(itemBox_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, itemBox_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(itemCount_));
  }
  glBindVertexArray(0);
}

}  // namespace world
