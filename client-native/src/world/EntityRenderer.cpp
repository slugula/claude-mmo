#include "world/EntityRenderer.hpp"

#include "world/GltfLoader.hpp"

#include <glm/gtc/matrix_transform.hpp>

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

void EntityRenderer::clearNpcKindModels() {
  for (auto& [kind, kit] : npcKindKits_) {
    for (auto& p : kit.prims) {
      if (p.outlineVao) glDeleteVertexArrays(1, &p.outlineVao);
      if (p.vao)        glDeleteVertexArrays(1, &p.vao);
      if (p.vboPos)     glDeleteBuffers(1, &p.vboPos);
      if (p.vboNrm)     glDeleteBuffers(1, &p.vboNrm);
      if (p.ebo)        glDeleteBuffers(1, &p.ebo);
    }
  }
  npcKindKits_.clear();
  npcKindSkinned_.clear();  // unique_ptrs free their own GL resources
}

void EntityRenderer::loadNpcKindModel(const std::string& kind,
                                      const std::filesystem::path& path) {
  // Animated if the glTF has clips → SkinnedMesh; else static instanced kit.
  auto sk = std::make_unique<world::SkinnedMesh>();
  if (sk->load(path) && sk->animationCount() > 0) {
    sk->setClip("");  // first clip, looping
    npcKindSkinned_[kind] = std::move(sk);
    std::fprintf(stdout, "[EntityRenderer] NPC '%s' loaded as ANIMATED model\n",
                 kind.c_str());
    return;
  }
  sk.reset();
  auto model = world::loadGlb(path);
  if (model && !model->primitives.empty())
    loadNpcKindModel(kind, model->primitives, model->materials);
}

void EntityRenderer::loadNpcKindModel(const std::string&                kind,
                                      const std::vector<GltfPrimitive>& primitives,
                                      const std::vector<GltfMaterial>&  materials)
{
  // Remove any existing kit for this kind.
  auto it = npcKindKits_.find(kind);
  if (it != npcKindKits_.end()) {
    for (auto& p : it->second.prims) {
      if (p.vao)    glDeleteVertexArrays(1, &p.vao);
      if (p.vboPos) glDeleteBuffers(1, &p.vboPos);
      if (p.vboNrm) glDeleteBuffers(1, &p.vboNrm);
      if (p.ebo)    glDeleteBuffers(1, &p.ebo);
    }
    npcKindKits_.erase(it);
  }

  CustomKit kit;
  for (const auto& prim : primitives) {
    if (prim.positions.empty() || prim.indices.empty()) continue;

    CustomPrim cp;
    cp.indexCount = static_cast<GLsizei>(prim.indices.size());
    if (prim.materialIndex >= 0 && prim.materialIndex < (int)materials.size())
      cp.color = materials[prim.materialIndex].baseColor;

    glCreateBuffers(1, &cp.vboPos);
    glNamedBufferStorage(cp.vboPos,
      prim.positions.size() * sizeof(float), prim.positions.data(), 0);

    std::vector<float> norms = prim.normals;
    if (norms.size() < prim.positions.size())
      norms.assign(prim.positions.size(), 0.0f);
    glCreateBuffers(1, &cp.vboNrm);
    glNamedBufferStorage(cp.vboNrm, norms.size() * sizeof(float), norms.data(), 0);

    glCreateBuffers(1, &cp.ebo);
    glNamedBufferStorage(cp.ebo,
      prim.indices.size() * sizeof(uint32_t), prim.indices.data(), 0);

    // VAO — same layout as Kit: positions@0, normals@1, instance@2+3.
    // Uses customNpcInstanceVbo_ so the same shader / draw call works.
    glCreateVertexArrays(1, &cp.vao);
    glVertexArrayVertexBuffer(cp.vao, 0, cp.vboPos, 0, sizeof(float) * 3);
    glEnableVertexArrayAttrib(cp.vao, 0);
    glVertexArrayAttribFormat(cp.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(cp.vao, 0, 0);

    glVertexArrayVertexBuffer(cp.vao, 1, cp.vboNrm, 0, sizeof(float) * 3);
    glEnableVertexArrayAttrib(cp.vao, 1);
    glVertexArrayAttribFormat(cp.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(cp.vao, 1, 1);

    glVertexArrayVertexBuffer(cp.vao, 2, customNpcInstanceVbo_, 0, sizeof(Instance));
    glVertexArrayBindingDivisor(cp.vao, 2, 1);
    glEnableVertexArrayAttrib(cp.vao, 2);
    glVertexArrayAttribFormat(cp.vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, x));
    glVertexArrayAttribBinding(cp.vao, 2, 2);
    glEnableVertexArrayAttrib(cp.vao, 3);
    glVertexArrayAttribFormat(cp.vao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Instance, rotY));
    glVertexArrayAttribBinding(cp.vao, 3, 2);

    glVertexArrayElementBuffer(cp.vao, cp.ebo);

    // Outline VAO: same geometry buffers, but driven by outlineInstanceVbo_
    // (the single-instance scratch VBO used by the mask/geometry pass).
    glCreateVertexArrays(1, &cp.outlineVao);
    glVertexArrayVertexBuffer(cp.outlineVao, 0, cp.vboPos, 0, sizeof(float) * 3);
    glEnableVertexArrayAttrib(cp.outlineVao, 0);
    glVertexArrayAttribFormat(cp.outlineVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(cp.outlineVao, 0, 0);

    glVertexArrayVertexBuffer(cp.outlineVao, 1, cp.vboNrm, 0, sizeof(float) * 3);
    glEnableVertexArrayAttrib(cp.outlineVao, 1);
    glVertexArrayAttribFormat(cp.outlineVao, 1, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(cp.outlineVao, 1, 1);

    glVertexArrayVertexBuffer(cp.outlineVao, 2, outlineInstanceVbo_, 0, sizeof(Instance));
    glVertexArrayBindingDivisor(cp.outlineVao, 2, 1);
    glEnableVertexArrayAttrib(cp.outlineVao, 2);
    glVertexArrayAttribFormat(cp.outlineVao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, x));
    glVertexArrayAttribBinding(cp.outlineVao, 2, 2);
    glEnableVertexArrayAttrib(cp.outlineVao, 3);
    glVertexArrayAttribFormat(cp.outlineVao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Instance, rotY));
    glVertexArrayAttribBinding(cp.outlineVao, 3, 2);

    glVertexArrayElementBuffer(cp.outlineVao, cp.ebo);

    kit.prims.push_back(cp);
  }

  if (!kit.prims.empty())
    npcKindKits_[kind] = std::move(kit);
}

void EntityRenderer::destroy() {
  clearNpcKindModels();
  if (npcOutlineVao_)      glDeleteVertexArrays(1, &npcOutlineVao_);
  if (itemOutlineVao_)     glDeleteVertexArrays(1, &itemOutlineVao_);
  if (outlineInstanceVbo_) glDeleteBuffers(1, &outlineInstanceVbo_);
  npcOutlineVao_ = itemOutlineVao_ = outlineInstanceVbo_ = 0;
  for (Kit* k : {&humanoid_, &itemBox_}) {
    if (k->vao)    glDeleteVertexArrays(1, &k->vao);
    if (k->ebo)    glDeleteBuffers(1, &k->ebo);
    if (k->vboNrm) glDeleteBuffers(1, &k->vboNrm);
    if (k->vboPos) glDeleteBuffers(1, &k->vboPos);
    *k = {};
  }
  if (npcInstanceVbo_)       glDeleteBuffers(1, &npcInstanceVbo_);
  if (itemInstanceVbo_)      glDeleteBuffers(1, &itemInstanceVbo_);
  if (customNpcInstanceVbo_) glDeleteBuffers(1, &customNpcInstanceVbo_);
  npcInstanceVbo_ = itemInstanceVbo_ = customNpcInstanceVbo_ = 0;
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
  glCreateBuffers(1, &customNpcInstanceVbo_);
  glNamedBufferStorage(npcInstanceVbo_,        sizeof(Instance) * kInstanceCap, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(itemInstanceVbo_,       sizeof(Instance) * kInstanceCap, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glNamedBufferStorage(customNpcInstanceVbo_,  sizeof(Instance) * kInstanceCap, nullptr, GL_DYNAMIC_STORAGE_BIT);

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

  // Single-instance VBO + per-kit outline VAOs for the 2-pass stencil outline.
  glCreateBuffers(1, &outlineInstanceVbo_);
  glNamedBufferStorage(outlineInstanceVbo_, sizeof(Instance), nullptr, GL_DYNAMIC_STORAGE_BIT);
  npcOutlineVao_  = buildOutlineVao(humanoid_);
  itemOutlineVao_ = buildOutlineVao(itemBox_);
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

void EntityRenderer::setNpcInstances(const std::vector<Instance>& insts,
                                      const std::vector<std::string>& kinds)
{
  const std::size_t n = std::min(insts.size(), kInstanceCap);
  // Store CPU copies for kind-based grouping in render().
  npcInstCpu_.assign(insts.begin(), insts.begin() + static_cast<ptrdiff_t>(n));
  npcKinds_   = kinds;
  if (npcKinds_.size() > n) npcKinds_.resize(n);
  // Upload everything to the default VBO (used for the humanoid fallback group).
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
    // Partition instances: custom-model kinds get their own per-kind draw;
    // everything else falls back to the humanoid procedural geometry.
    std::unordered_map<std::string, std::vector<Instance>> kindGroups;
    std::vector<Instance> humanoidGroup;
    humanoidGroup.reserve(npcCount_);

    for (std::size_t i = 0; i < npcCount_; ++i) {
      const std::string& k = (i < npcKinds_.size()) ? npcKinds_[i] : "";
      if (isAnimatedKind(k))            continue;  // drawn in renderAnimatedNpcs()
      else if (!k.empty() && npcKindKits_.count(k))
        kindGroups[k].push_back(npcInstCpu_[i]);
      else
        humanoidGroup.push_back(npcInstCpu_[i]);
    }

    // Humanoid fallback group.
    if (!humanoidGroup.empty()) {
      glNamedBufferSubData(npcInstanceVbo_, 0,
        humanoidGroup.size() * sizeof(Instance), humanoidGroup.data());
      shader.setVec3("u_color", humanoid_.color);
      glBindVertexArray(humanoid_.vao);
      glDrawElementsInstanced(GL_TRIANGLES, humanoid_.indexCount, GL_UNSIGNED_INT,
                              nullptr, static_cast<GLsizei>(humanoidGroup.size()));
    }

    // Custom-model groups.
    for (auto& [kind, insts] : kindGroups) {
      const std::size_t cnt = std::min(insts.size(), kInstanceCap);
      glNamedBufferSubData(customNpcInstanceVbo_, 0,
        cnt * sizeof(Instance), insts.data());
      const CustomKit& ckit = npcKindKits_.at(kind);
      for (const auto& cp : ckit.prims) {
        shader.setVec3("u_color", glm::vec3(cp.color));
        glBindVertexArray(cp.vao);
        glDrawElementsInstanced(GL_TRIANGLES, cp.indexCount, GL_UNSIGNED_INT,
                                nullptr, static_cast<GLsizei>(cnt));
      }
    }
  }
  if (itemCount_ > 0) {
    shader.setVec3("u_color", itemBox_.color);
    glBindVertexArray(itemBox_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, itemBox_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(itemCount_));
  }
  glBindVertexArray(0);
}

void EntityRenderer::renderAnimatedNpcs(render::Shader& skinnedShader, float dt) {
  if (npcKindSkinned_.empty() || npcCount_ == 0) return;

  // Group instance indices by animated kind so each clip advances once.
  std::unordered_map<std::string, std::vector<const Instance*>> groups;
  for (std::size_t i = 0; i < npcCount_; ++i) {
    const std::string& k = (i < npcKinds_.size()) ? npcKinds_[i] : "";
    if (isAnimatedKind(k)) groups[k].push_back(&npcInstCpu_[i]);
  }

  for (auto& [kind, insts] : groups) {
    auto it = npcKindSkinned_.find(kind);
    if (it == npcKindSkinned_.end() || !it->second) continue;
    world::SkinnedMesh& mesh = *it->second;
    mesh.update(dt);                       // advance this kind's clip once
    for (const Instance* in : insts) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(in->x, in->y, in->z));
      m = glm::rotate(m, in->rotY, glm::vec3(0.0f, 1.0f, 0.0f));
      mesh.render(skinnedShader, m, /*useMaterialColors=*/true);
    }
  }
}

void EntityRenderer::renderDepth(render::Shader& /*shader*/) {
  // Render NPC geometry into the shadow depth buffer using the same VAOs as
  // render(). The caller has already set u_lightViewProj; no color uniforms
  // are needed for a depth-only pass.
  if (npcCount_ == 0) return;

  std::vector<Instance> humanoidGroup;
  std::unordered_map<std::string, std::vector<Instance>> kindGroups;
  humanoidGroup.reserve(npcCount_);

  for (std::size_t i = 0; i < npcCount_; ++i) {
    const std::string& k = (i < npcKinds_.size()) ? npcKinds_[i] : "";
    if (isAnimatedKind(k))            continue;  // animated NPCs skip the shadow pass for now
    else if (!k.empty() && npcKindKits_.count(k))
      kindGroups[k].push_back(npcInstCpu_[i]);
    else
      humanoidGroup.push_back(npcInstCpu_[i]);
  }

  if (!humanoidGroup.empty()) {
    glNamedBufferSubData(npcInstanceVbo_, 0,
      humanoidGroup.size() * sizeof(Instance), humanoidGroup.data());
    glBindVertexArray(humanoid_.vao);
    glDrawElementsInstanced(GL_TRIANGLES, humanoid_.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(humanoidGroup.size()));
  }

  for (auto& [kind, insts] : kindGroups) {
    const std::size_t cnt = std::min(insts.size(), kInstanceCap);
    glNamedBufferSubData(customNpcInstanceVbo_, 0,
      cnt * sizeof(Instance), insts.data());
    const CustomKit& ckit = npcKindKits_.at(kind);
    for (const auto& cp : ckit.prims) {
      glBindVertexArray(cp.vao);
      glDrawElementsInstanced(GL_TRIANGLES, cp.indexCount, GL_UNSIGNED_INT,
                              nullptr, static_cast<GLsizei>(cnt));
    }
  }

  glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Outline helpers
// ---------------------------------------------------------------------------

GLuint EntityRenderer::buildOutlineVao(const Kit& kit) const {
  GLuint vao = 0;
  glCreateVertexArrays(1, &vao);
  // position (binding 0)
  glVertexArrayVertexBuffer(vao, 0, kit.vboPos, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(vao, 0);
  glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(vao, 0, 0);
  // normal (binding 1)
  glVertexArrayVertexBuffer(vao, 1, kit.vboNrm, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(vao, 1);
  glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(vao, 1, 1);
  // single instance — pos xyz (binding 2, divisor 1)
  glVertexArrayVertexBuffer(vao, 2, outlineInstanceVbo_, 0, sizeof(Instance));
  glVertexArrayBindingDivisor(vao, 2, 1);
  glEnableVertexArrayAttrib(vao, 2);
  glVertexArrayAttribFormat(vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, x));
  glVertexArrayAttribBinding(vao, 2, 2);
  // single instance — rotY (binding 2, offset 12)
  glEnableVertexArrayAttrib(vao, 3);
  glVertexArrayAttribFormat(vao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Instance, rotY));
  glVertexArrayAttribBinding(vao, 3, 2);
  // index buffer
  glVertexArrayElementBuffer(vao, kit.ebo);
  return vao;
}

void EntityRenderer::doOutline2Pass(render::Shader& shader,
                                    GLuint   vao,
                                    GLsizei  indexCount,
                                    const glm::mat4& viewProj,
                                    const Instance&  inst,
                                    const glm::vec4& color,
                                    float    outlineWidth) const {
  // Upload the single instance we want to outline.
  glNamedBufferSubData(outlineInstanceVbo_, 0, sizeof(Instance), &inst);

  shader.use();
  shader.setMat4("u_viewProj",     viewProj);
  shader.setVec4("u_outlineColor", color);

  // ---------- Save state -----------------------------------------------
  GLboolean oldCullEn, oldDepthWr;
  glGetBooleanv(GL_CULL_FACE,       &oldCullEn);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthWr);
  GLint oldCullFace;
  glGetIntegerv(GL_CULL_FACE_MODE,  &oldCullFace);

  // ---------- Pass 1: stamp stencil, no colour output -------------------
  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glClear(GL_STENCIL_BUFFER_BIT);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glDepthMask(GL_FALSE);

  shader.setFloat("u_outlineWidth", 0.0f);
  glBindVertexArray(vao);
  glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr, 1);

  // ---------- Pass 2: only pixels NOT in stencil = outline ring ---------
  glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glCullFace(GL_FRONT);  // back-shell only

  shader.setFloat("u_outlineWidth", outlineWidth);
  glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr, 1);

  glBindVertexArray(0);

  // ---------- Restore state ---------------------------------------------
  glCullFace(static_cast<GLenum>(oldCullFace));
  if (!oldCullEn) glDisable(GL_CULL_FACE);
  glDepthMask(oldDepthWr);
  glStencilMask(0xFF);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void EntityRenderer::renderNpcOutline(render::Shader& outlineShader,
                                      const glm::mat4& viewProj,
                                      const Instance&  inst,
                                      const glm::vec4& color) const {
  if (!npcOutlineVao_ || !outlineInstanceVbo_) return;
  doOutline2Pass(outlineShader, npcOutlineVao_, humanoid_.indexCount,
                 viewProj, inst, color, 0.07f);
}

void EntityRenderer::renderItemOutline(render::Shader& outlineShader,
                                       const glm::mat4& viewProj,
                                       const Instance&  inst,
                                       const glm::vec4& color) const {
  if (!itemOutlineVao_ || !outlineInstanceVbo_) return;
  doOutline2Pass(outlineShader, itemOutlineVao_, itemBox_.indexCount,
                 viewProj, inst, color, 0.12f);
}

void EntityRenderer::renderNpcGeometry(render::Shader& /*maskShader*/,
                                       const Instance& inst,
                                       const std::string& kind) const {
  if (!outlineInstanceVbo_) return;
  glNamedBufferSubData(outlineInstanceVbo_, 0, sizeof(Instance), &inst);
  glDisable(GL_STENCIL_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);

  // Use the custom model's outline VAOs if one is loaded for this kind.
  auto it = npcKindKits_.find(kind);
  if (it != npcKindKits_.end() && !it->second.prims.empty()) {
    for (const auto& cp : it->second.prims) {
      if (!cp.outlineVao) continue;
      glBindVertexArray(cp.outlineVao);
      glDrawElementsInstanced(GL_TRIANGLES, cp.indexCount, GL_UNSIGNED_INT, nullptr, 1);
    }
  } else if (npcOutlineVao_) {
    glBindVertexArray(npcOutlineVao_);
    glDrawElementsInstanced(GL_TRIANGLES, humanoid_.indexCount, GL_UNSIGNED_INT, nullptr, 1);
  }

  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
}

void EntityRenderer::renderItemGeometry(render::Shader& /*maskShader*/,
                                        const Instance& inst) const {
  if (!itemOutlineVao_ || !outlineInstanceVbo_) return;
  glNamedBufferSubData(outlineInstanceVbo_, 0, sizeof(Instance), &inst);
  glDisable(GL_STENCIL_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  glBindVertexArray(itemOutlineVao_);
  glDrawElementsInstanced(GL_TRIANGLES, itemBox_.indexCount, GL_UNSIGNED_INT, nullptr, 1);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
}

}  // namespace world
