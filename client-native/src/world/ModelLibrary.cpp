#include "world/ModelLibrary.hpp"

#include "world/GltfLoader.hpp"
#include "input/Picker.hpp"

#include <algorithm>
#include <cstdio>

namespace world {

namespace {
// Built-in unit cube (base at Y=0, ±0.5 in X/Z, 0..1 in Y) used when a
// placeholder model file is absent so nothing ever renders as "nothing".
void makeUnitCube(std::vector<float>& pos, std::vector<float>& nrm,
                  std::vector<uint32_t>& idx) {
  pos.clear(); nrm.clear(); idx.clear();
  auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
    const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
    for (const glm::vec3& p : {a, b, c, d}) {
      pos.insert(pos.end(), { p.x, p.y, p.z });
      nrm.insert(nrm.end(), { n.x, n.y, n.z });
    }
    idx.insert(idx.end(), { base, base+1, base+2, base, base+2, base+3 });
  };
  const float h = 0.5f, y0 = 0.0f, y1 = 1.0f;
  face({-h,y0,-h},{-h,y0, h},{-h,y1, h},{-h,y1,-h},{-1,0,0});
  face({ h,y0, h},{ h,y0,-h},{ h,y1,-h},{ h,y1, h},{ 1,0,0});
  face({ h,y0,-h},{-h,y0,-h},{-h,y1,-h},{ h,y1,-h},{ 0,0,-1});
  face({-h,y0, h},{ h,y0, h},{ h,y1, h},{-h,y1, h},{ 0,0, 1});
  face({-h,y1, h},{ h,y1, h},{ h,y1,-h},{-h,y1,-h},{ 0,1,0});
  face({-h,y0,-h},{ h,y0,-h},{ h,y0, h},{-h,y0, h},{ 0,-1,0});
}
}  // namespace

ModelLibrary::~ModelLibrary() { destroy(); }

void ModelLibrary::init(std::function<std::filesystem::path(const std::string&)> resolver,
                        const std::string& placeholderRelPath) {
  resolver_ = std::move(resolver);

  if (!scratchVbo_) {
    glCreateBuffers(1, &scratchVbo_);
    glNamedBufferStorage(scratchVbo_, sizeof(Instance) * kInstanceCap,
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
  }

  // Load the placeholder model into CPU arrays. Fall back to a unit cube.
  bool loaded = false;
  if (resolver_ && !placeholderRelPath.empty()) {
    const auto path = resolver_(placeholderRelPath);
    if (std::filesystem::exists(path)) {
      if (auto m = world::loadGlb(path); m && !m->primitives.empty()) {
        phPos_.clear(); phNrm_.clear(); phIdx_.clear();
        uint32_t base = 0;
        for (const auto& prim : m->primitives) {
          phPos_.insert(phPos_.end(), prim.positions.begin(), prim.positions.end());
          std::vector<float> n = prim.normals;
          if (n.size() < prim.positions.size()) n.assign(prim.positions.size(), 0.f);
          phNrm_.insert(phNrm_.end(), n.begin(), n.end());
          for (uint32_t i : prim.indices) phIdx_.push_back(base + i);
          base += static_cast<uint32_t>(prim.positions.size() / 3);
        }
        loaded = true;
        std::fprintf(stdout, "[ModelLibrary] placeholder loaded: %s\n",
                     placeholderRelPath.c_str());
      }
    }
  }
  if (!loaded) {
    makeUnitCube(phPos_, phNrm_, phIdx_);
    std::fprintf(stderr, "[ModelLibrary] placeholder '%s' missing — using unit cube\n",
                 placeholderRelPath.c_str());
  }
}

void ModelLibrary::destroyKit(Kit& k) {
  if (k.vao)    glDeleteVertexArrays(1, &k.vao);
  if (k.ebo)    glDeleteBuffers(1, &k.ebo);
  if (k.vboCol) glDeleteBuffers(1, &k.vboCol);
  if (k.vboNrm) glDeleteBuffers(1, &k.vboNrm);
  if (k.vboPos) glDeleteBuffers(1, &k.vboPos);
  k = {};
}

void ModelLibrary::clearEntries() {
  for (auto& [id, e] : entries_)
    for (Kit& k : e.staticKits) destroyKit(k);
  entries_.clear();
}

void ModelLibrary::destroy() {
  clearEntries();
  if (scratchVbo_) { glDeleteBuffers(1, &scratchVbo_); scratchVbo_ = 0; }
}

void ModelLibrary::uploadKit(Kit& k, const std::vector<float>& pos,
                             const std::vector<float>& nrm,
                             const std::vector<float>& col,
                             const std::vector<uint32_t>& idx, glm::vec3 color) {
  k.color      = color;
  k.indexCount = static_cast<GLsizei>(idx.size());

  // Per-vertex RGBA colors; default to white when the model has none so the
  // shader's (u_color * vColor) leaves the material colour unchanged.
  const std::size_t vcount = pos.size() / 3;
  std::vector<float> colors;
  if (col.size() == vcount * 4) {
    colors = col;
  } else {
    colors.assign(vcount * 4, 1.0f);
  }

  glCreateBuffers(1, &k.vboPos);
  glCreateBuffers(1, &k.vboNrm);
  glCreateBuffers(1, &k.vboCol);
  glCreateBuffers(1, &k.ebo);
  glNamedBufferStorage(k.vboPos, static_cast<GLsizeiptr>(pos.size() * sizeof(float)), pos.data(), 0);
  glNamedBufferStorage(k.vboNrm, static_cast<GLsizeiptr>(nrm.size() * sizeof(float)), nrm.data(), 0);
  glNamedBufferStorage(k.vboCol, static_cast<GLsizeiptr>(colors.size() * sizeof(float)), colors.data(), 0);
  glNamedBufferStorage(k.ebo,    static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)), idx.data(), 0);

  glCreateVertexArrays(1, &k.vao);
  // location 0 = position (per-vertex)
  glVertexArrayVertexBuffer (k.vao, 0, k.vboPos, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib (k.vao, 0);
  glVertexArrayAttribFormat (k.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(k.vao, 0, 0);
  // location 1 = normal (per-vertex)
  glVertexArrayVertexBuffer (k.vao, 1, k.vboNrm, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib (k.vao, 1);
  glVertexArrayAttribFormat (k.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(k.vao, 1, 1);
  // location 4 = per-vertex RGBA colour (binding 4)
  glVertexArrayVertexBuffer (k.vao, 4, k.vboCol, 0, sizeof(float) * 4);
  glEnableVertexArrayAttrib (k.vao, 4);
  glVertexArrayAttribFormat (k.vao, 4, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(k.vao, 4, 4);
  // locations 2,3 = per-instance pos + rotY (binding 2 = shared scratch VBO)
  glVertexArrayVertexBuffer  (k.vao, 2, scratchVbo_, 0, sizeof(Instance));
  glVertexArrayBindingDivisor(k.vao, 2, 1);
  glEnableVertexArrayAttrib  (k.vao, 2);
  glVertexArrayAttribFormat  (k.vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, x));
  glVertexArrayAttribBinding (k.vao, 2, 2);
  glEnableVertexArrayAttrib  (k.vao, 3);
  glVertexArrayAttribFormat  (k.vao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Instance, rotY));
  glVertexArrayAttribBinding (k.vao, 3, 2);
  // location 5 = per-instance surface up-normal (binding 2 = shared scratch VBO)
  glEnableVertexArrayAttrib  (k.vao, 5);
  glVertexArrayAttribFormat  (k.vao, 5, 3, GL_FLOAT, GL_FALSE, offsetof(Instance, nx));
  glVertexArrayAttribBinding (k.vao, 5, 2);

  glVertexArrayElementBuffer(k.vao, k.ebo);
}

void ModelLibrary::ensure(const std::string& id, const std::string& modelPath,
                          int sizeX, int sizeY) {
  if (entries_.count(id)) return;   // already loaded (call clearEntries() to reload)

  Entry e;

  // Resolve the model file (try as-is, then with assets/ prefix).
  std::filesystem::path path;
  if (resolver_ && !modelPath.empty()) {
    path = resolver_(modelPath);
    if (!std::filesystem::exists(path)) path = resolver_("assets/" + modelPath);
  }
  const bool haveFile = !path.empty() && std::filesystem::exists(path);

  glm::vec3 bmin( 1e9f), bmax(-1e9f);
  auto accumulate = [&](const std::vector<float>& pos) {
    for (std::size_t i = 0; i + 2 < pos.size(); i += 3) {
      bmin = glm::min(bmin, glm::vec3(pos[i], pos[i+1], pos[i+2]));
      bmax = glm::max(bmax, glm::vec3(pos[i], pos[i+1], pos[i+2]));
    }
  };

  if (haveFile) {
    auto model = world::loadGlb(path);
    if (model && !model->primitives.empty()) {
      for (const auto& prim : model->primitives) accumulate(prim.positions);
      if (!model->animations.empty()) {
        // Animated → SkinnedMesh.
        auto sk = std::make_unique<SkinnedMesh>();
        if (sk->load(path)) {
          sk->setClip("");
          e.animated = true;
          e.skinned  = std::move(sk);
        }
      }
      if (!e.animated) {
        // Static → one kit per primitive (preserve material colour).
        for (const auto& prim : model->primitives) {
          if (prim.positions.empty() || prim.indices.empty()) continue;
          std::vector<float> n = prim.normals;
          if (n.size() < prim.positions.size()) n.assign(prim.positions.size(), 0.f);
          glm::vec3 col = (prim.materialIndex >= 0 &&
                           prim.materialIndex < (int)model->materials.size())
                          ? glm::vec3(model->materials[prim.materialIndex].baseColor)
                          : glm::vec3(0.7f);
          Kit k; uploadKit(k, prim.positions, n, prim.colors, prim.indices, col);
          e.staticKits.push_back(k);
          // Retain merged CPU geometry for narrow-phase ray picking.
          const unsigned int base = static_cast<unsigned int>(e.cpuPos.size() / 3);
          e.cpuPos.insert(e.cpuPos.end(), prim.positions.begin(), prim.positions.end());
          for (unsigned int idx : prim.indices) e.cpuIdx.push_back(base + idx);
        }
      }
    }
  }

  // No file (or load failed) → placeholder static kit.
  if (!e.animated && e.staticKits.empty()) {
    accumulate(phPos_);
    Kit k; uploadKit(k, phPos_, phNrm_, {}, phIdx_, glm::vec3(0.85f, 0.20f, 0.85f)); // magenta placeholder
    e.staticKits.push_back(k);
  }

  if (bmin.x > bmax.x) { bmin = glm::vec3(-0.5f, 0.f, -0.5f); bmax = glm::vec3(0.5f, 1.f, 0.5f); }

  // Union the model bounds with the footprint extent in XZ. Footprint is
  // anchored bottom-left at the origin tile: x,z ∈ [−0.5, size−0.5].
  const float fx = static_cast<float>(std::max(1, sizeX));
  const float fy = static_cast<float>(std::max(1, sizeY));
  bmin.x = std::min(bmin.x, -0.5f);          bmin.z = std::min(bmin.z, -0.5f);
  bmax.x = std::max(bmax.x, fx - 0.5f);      bmax.z = std::max(bmax.z, fy - 0.5f);
  // Guarantee a pickable vertical extent. Thin or oddly-baked models (e.g.
  // node-animated props whose rest pose sits near a single Y plane) otherwise
  // collapse to a near-flat box the pick ray skims past, making them unclickable.
  bmin.y = std::min(bmin.y, 0.0f);
  bmax.y = std::max(bmax.y, bmin.y + 1.0f);
  e.aabbMin = bmin;
  e.aabbMax = bmax;

  entries_.emplace(id, std::move(e));
}

bool ModelLibrary::isAnimated(const std::string& id) const {
  auto it = entries_.find(id);
  return it != entries_.end() && it->second.animated;
}

bool ModelLibrary::aabb(const std::string& id, glm::vec3& outMin, glm::vec3& outMax) const {
  auto it = entries_.find(id);
  if (it == entries_.end()) return false;
  outMin = it->second.aabbMin;
  outMax = it->second.aabbMax;
  return true;
}

bool ModelLibrary::rayHitLocal(const std::string& id, const glm::vec3& ro,
                               const glm::vec3& rd, float& tHit) const {
  auto it = entries_.find(id);
  if (it == entries_.end()) return false;
  const Entry& e = it->second;
  if (e.cpuPos.size() < 9 || e.cpuIdx.size() < 3) return false;  // no static geom

  auto vtx = [&](unsigned int i) {
    return glm::vec3(e.cpuPos[i * 3], e.cpuPos[i * 3 + 1], e.cpuPos[i * 3 + 2]);
  };
  bool  hit  = false;
  float best = 1e30f;
  for (std::size_t i = 0; i + 2 < e.cpuIdx.size(); i += 3) {
    float t;
    if (input::rayTriangle(ro, rd, vtx(e.cpuIdx[i]), vtx(e.cpuIdx[i + 1]),
                           vtx(e.cpuIdx[i + 2]), &t) && t < best) {
      best = t; hit = true;
    }
  }
  if (hit) tHit = best;
  return hit;
}

int ModelLibrary::rayHitWorld(const std::string& id, const glm::mat4& world,
                              const glm::vec3& ro, const glm::vec3& rd,
                              float& tHit) const {
  auto it = entries_.find(id);
  if (it == entries_.end() || it->second.cpuPos.size() < 9) return -1;  // no geom
  const glm::mat4 inv = glm::inverse(world);
  const glm::vec3 lro = glm::vec3(inv * glm::vec4(ro, 1.0f));
  const glm::vec3 lrd = glm::vec3(inv * glm::vec4(rd, 0.0f));  // unnormalized → t stays world-comparable
  return rayHitLocal(id, lro, lrd, tHit) ? 1 : 0;
}

void ModelLibrary::update(float dt) {
  for (auto& [id, e] : entries_)
    if (e.animated && e.skinned) e.skinned->update(dt);
}

void ModelLibrary::drawStaticInstanced(render::Shader& shader, const std::string& id,
                                       const std::vector<Instance>& instances) {
  if (instances.empty()) return;
  auto it = entries_.find(id);
  if (it == entries_.end() || it->second.animated) return;

  const std::size_t count = std::min(instances.size(), kInstanceCap);
  glNamedBufferSubData(scratchVbo_, 0,
      static_cast<GLsizeiptr>(count * sizeof(Instance)), instances.data());
  for (const Kit& k : it->second.staticKits) {
    if (!k.vao) continue;
    shader.setVec3("u_color", k.color);
    glBindVertexArray(k.vao);
    glDrawElementsInstanced(GL_TRIANGLES, k.indexCount, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(count));
  }
  glBindVertexArray(0);
}

void ModelLibrary::drawAnimatedAt(render::Shader& shader, const std::string& id,
                                  const glm::mat4& model) {
  auto it = entries_.find(id);
  if (it == entries_.end() || !it->second.animated || !it->second.skinned) return;
  it->second.skinned->render(shader, model, /*useMaterialColors=*/true);
}

}  // namespace world
