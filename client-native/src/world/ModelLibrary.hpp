#pragma once

#include "render/Shader.hpp"
#include "world/SkinnedMesh.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace world {

// =====================================================================
// ModelLibrary — shared, data-driven model registry keyed by entity id.
// =====================================================================
//
// Owned by both ObstacleSystem (objects) and EntityRenderer (NPCs). Each
// definition's model_path is loaded once; a model with glTF animations is
// kept as a SkinnedMesh (animated), otherwise uploaded as static instanced
// geometry. Any id with an empty/missing path falls back to a per-type
// placeholder model (or a built-in unit cube if the placeholder file is
// absent), so content always renders something.
//
// Instance sources stay with the caller (obstacles from map tiles, NPCs from
// server state). The library exposes:
//   - aabb(id)                  → world-space bounds for picking/outline
//   - update(dt)                → advance all animated clips once per frame
//   - drawStaticInstanced(...)  → instanced draw of one id's static kits
//   - drawAnimatedAt(...)       → one skinned draw at a transform
class ModelLibrary {
public:
  // Per-instance payload (matches the obstacle/entity shader instance layout).
  // (nx,ny,nz) is the surface up-normal the model is tilted onto — defaults to
  // straight up, so existing {x,y,z,rotY} brace-inits leave upright geometry
  // (trees/rocks/NPCs) unchanged; dropped flat models set it to the tile normal.
  struct Instance {
    float x, y, z, rotY;
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
  };

  ModelLibrary() = default;
  ~ModelLibrary();
  ModelLibrary(const ModelLibrary&)            = delete;
  ModelLibrary& operator=(const ModelLibrary&) = delete;

  // resolver maps a relative path → absolute path (host knows the exe dir).
  // placeholderRelPath is this library's fallback model (object vs npc).
  void init(std::function<std::filesystem::path(const std::string&)> resolver,
            const std::string& placeholderRelPath);
  void destroy();

  // Load/cache the model for `id`. Empty or missing modelPath → placeholder.
  // footprint (sizeX × sizeY tiles) widens the picking AABB.
  void ensure(const std::string& id, const std::string& modelPath,
              int sizeX = 1, int sizeY = 1);
  void clearEntries();   // drop per-id entries (keeps placeholder + scratch VBO)

  bool has(const std::string& id) const { return entries_.count(id) > 0; }
  bool isAnimated(const std::string& id) const;

  // World-space (model-local) AABB for the id. Returns false if unknown.
  bool aabb(const std::string& id, glm::vec3& outMin, glm::vec3& outMax) const;

  // Narrow-phase ray-vs-mesh pick in MODEL-LOCAL space (caller transforms the
  // world ray by the instance's inverse transform first). Returns the nearest
  // hit's parametric t. Static models only (animated → false, use AABB).
  bool rayHitLocal(const std::string& id, const glm::vec3& ro,
                   const glm::vec3& rd, float& tHit) const;

  // Tri-state narrow-phase pick using the instance's world matrix:
  //   1 = mesh hit (writes tHit), 0 = has geom but ray missed, -1 = no precise
  //   geom (caller should fall back to the AABB result).
  int rayHitWorld(const std::string& id, const glm::mat4& world,
                  const glm::vec3& ro, const glm::vec3& rd, float& tHit) const;

  // Advance every animated entry's clip by dt once per frame.
  void update(float dt);

  // Static instanced draw: uploads `instances` to the shared scratch VBO and
  // draws each of this id's static kits. Caller sets all shader uniforms
  // except u_color (set per-kit here). No-op for animated ids.
  void drawStaticInstanced(render::Shader& shader, const std::string& id,
                           const std::vector<Instance>& instances);

  // Skinned draw of one animated instance at `model`. No-op for static ids.
  void drawAnimatedAt(render::Shader& shader, const std::string& id,
                      const glm::mat4& model);

private:
  struct Kit {
    GLuint    vao        = 0;
    GLuint    vboPos     = 0;
    GLuint    vboNrm     = 0;
    GLuint    vboCol     = 0;   // per-vertex RGBA (location 4)
    GLuint    ebo        = 0;
    GLsizei   indexCount = 0;
    glm::vec3 color      = glm::vec3(0.7f);
  };
  struct Entry {
    bool                          animated = false;
    std::vector<Kit>              staticKits;   // owned
    std::unique_ptr<SkinnedMesh>  skinned;      // owned (animated)
    glm::vec3                     aabbMin = glm::vec3(-0.5f, 0.0f, -0.5f);
    glm::vec3                     aabbMax = glm::vec3( 0.5f, 1.0f,  0.5f);
    // Merged static geometry (model space) retained for narrow-phase ray picking.
    std::vector<float>            cpuPos;   // x,y,z per vertex
    std::vector<unsigned int>     cpuIdx;   // triangle indices into cpuPos
  };

  // Upload one kit (positions/normals/indices) wired to scratchVbo_ at the
  // per-instance binding. Defined in the .cpp.
  void uploadKit(Kit& k, const std::vector<float>& pos,
                 const std::vector<float>& nrm,
                 const std::vector<float>& col,   // RGBA per vertex; empty → white
                 const std::vector<uint32_t>& idx, glm::vec3 color);
  void destroyKit(Kit& k);

  std::function<std::filesystem::path(const std::string&)> resolver_;
  std::unordered_map<std::string, Entry> entries_;

  // Shared scratch instance VBO that every static kit's VAO binds to; one id's
  // instances are uploaded immediately before its draw.
  GLuint scratchVbo_ = 0;
  static constexpr std::size_t kInstanceCap = 4096;

  // Placeholder CPU mesh (loaded in init; falls back to a unit cube).
  std::vector<float>    phPos_;
  std::vector<float>    phNrm_;
  std::vector<uint32_t> phIdx_;
};

}  // namespace world
