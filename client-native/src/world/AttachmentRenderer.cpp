#include "world/AttachmentRenderer.hpp"

#include "assets/AssetPack.hpp"
#include "world/GltfLoader.hpp"
#include "world/GltfModel.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

namespace world {

// ---------------------------------------------------------------------------
glm::mat4 gripMatrix(const glm::vec3& posOffset,
                     const glm::vec3& rotEulerDeg,
                     float scale) {
  glm::mat4 m(1.0f);
  m = glm::translate(m, posOffset);
  m = glm::rotate(m, glm::radians(rotEulerDeg.z), glm::vec3(0, 0, 1));
  m = glm::rotate(m, glm::radians(rotEulerDeg.y), glm::vec3(0, 1, 0));
  m = glm::rotate(m, glm::radians(rotEulerDeg.x), glm::vec3(1, 0, 0));
  m = glm::scale(m, glm::vec3(scale <= 0.0f ? 1.0f : scale));
  return m;
}

// ---------------------------------------------------------------------------
bool AttachmentRenderer::init(
    const std::string& vertPath, const std::string& fragPath,
    std::function<std::filesystem::path(const std::string&)> resolver) {
  resolver_ = std::move(resolver);
  if (!shader_.fromFiles(vertPath, fragPath)) {
    std::fprintf(stderr, "[AttachmentRenderer] preview shader load failed\n");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
void AttachmentRenderer::destroy() {
  for (auto& [path, model] : cache_) {
    for (auto& p : model.prims) {
      if (p.ebo)    glDeleteBuffers(1, &p.ebo);
      if (p.vboCol) glDeleteBuffers(1, &p.vboCol);
      if (p.vboNrm) glDeleteBuffers(1, &p.vboNrm);
      if (p.vboPos) glDeleteBuffers(1, &p.vboPos);
      if (p.vao)    glDeleteVertexArrays(1, &p.vao);
    }
  }
  cache_.clear();
}

// ---------------------------------------------------------------------------
const AttachmentRenderer::Model&
AttachmentRenderer::ensure(const std::string& relPath) {
  auto it = cache_.find(relPath);
  if (it != cache_.end()) return it->second;

  Model model;
  model.attempted = true;

  std::filesystem::path abs = resolver_ ? resolver_(relPath)
                                        : std::filesystem::path(relPath);
  if (!assets::exists(abs)) abs = resolver_ ? resolver_("assets/" + relPath)
                                            : abs;

  if (assets::exists(abs)) {
    if (auto parsed = world::loadGlb(abs); parsed && !parsed->primitives.empty()) {
      for (const auto& prim : parsed->primitives) {
        if (prim.positions.empty() || prim.indices.empty()) continue;
        Prim gp;
        gp.indexCount = static_cast<GLsizei>(prim.indices.size());
        if (prim.materialIndex >= 0 &&
            prim.materialIndex < static_cast<int>(parsed->materials.size()))
          gp.color = parsed->materials[prim.materialIndex].baseColor;

        glCreateBuffers(1, &gp.vboPos);
        glNamedBufferStorage(gp.vboPos,
            static_cast<GLsizeiptr>(prim.positions.size() * sizeof(float)),
            prim.positions.data(), 0);

        std::vector<float> norms = prim.normals;
        if (norms.size() < prim.positions.size())
          norms.assign(prim.positions.size(), 0.f);
        glCreateBuffers(1, &gp.vboNrm);
        glNamedBufferStorage(gp.vboNrm,
            static_cast<GLsizeiptr>(norms.size() * sizeof(float)), norms.data(), 0);

        const std::size_t vcount = prim.positions.size() / 3;
        std::vector<float> cols = prim.colors;
        if (cols.size() != vcount * 4) cols.assign(vcount * 4, 1.0f);
        glCreateBuffers(1, &gp.vboCol);
        glNamedBufferStorage(gp.vboCol,
            static_cast<GLsizeiptr>(cols.size() * sizeof(float)), cols.data(), 0);

        glCreateBuffers(1, &gp.ebo);
        glNamedBufferStorage(gp.ebo,
            static_cast<GLsizeiptr>(prim.indices.size() * sizeof(uint32_t)),
            prim.indices.data(), 0);

        glCreateVertexArrays(1, &gp.vao);
        glVertexArrayVertexBuffer(gp.vao, 0, gp.vboPos, 0, 3 * sizeof(float));
        glEnableVertexArrayAttrib(gp.vao, 0);
        glVertexArrayAttribFormat(gp.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(gp.vao, 0, 0);
        glVertexArrayVertexBuffer(gp.vao, 1, gp.vboNrm, 0, 3 * sizeof(float));
        glEnableVertexArrayAttrib(gp.vao, 1);
        glVertexArrayAttribFormat(gp.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(gp.vao, 1, 1);
        glVertexArrayVertexBuffer(gp.vao, 4, gp.vboCol, 0, 4 * sizeof(float));
        glEnableVertexArrayAttrib(gp.vao, 4);
        glVertexArrayAttribFormat(gp.vao, 4, 4, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(gp.vao, 4, 4);
        glVertexArrayElementBuffer(gp.vao, gp.ebo);

        model.prims.push_back(gp);
      }
    } else {
      std::fprintf(stderr,
        "[AttachmentRenderer] '%s' parsed 0 primitives (missing .bin / external "
        "buffer?) — weapon will not render\n", abs.string().c_str());
    }
  } else {
    std::fprintf(stderr, "[AttachmentRenderer] model not found: %s\n", relPath.c_str());
  }

  auto [ins, _] = cache_.emplace(relPath, std::move(model));
  return ins->second;
}

// ---------------------------------------------------------------------------
void AttachmentRenderer::draw(const std::string& relPath,
                              const glm::mat4& world, const glm::mat4& viewProj) {
  if (relPath.empty() || !shader_.isValid()) return;
  const Model& model = ensure(relPath);
  if (model.prims.empty()) return;

  shader_.use();
  shader_.setMat4("u_model",    world);
  shader_.setMat4("u_viewProj", viewProj);
  for (const auto& p : model.prims) {
    shader_.setVec4("u_color", p.color);
    glBindVertexArray(p.vao);
    glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

}  // namespace world
