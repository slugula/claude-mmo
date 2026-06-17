#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "world/GltfModel.hpp"
#include "world/ModelLibrary.hpp"
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

// Renders NPCs and dropped items as instanced procedural geometry.
//
// Phase 5d MVP: every NPC is a small humanoid (cylinder body + sphere head),
// every dropped item is a small box. Both share the obstacle shader so they
// get the same Lambert + HSL palette quantization as the rest of the world.
// Per-NPC type differentiation (chicken vs shopkeeper geometry) and
// per-item appearance can come later — for now they're flat-shaded by color.
//
class EntityRenderer {
public:
  // Per-instance attribute payload: world XYZ + Y-axis rotation. Public so
  // callers (App) can build interpolated arrays directly.
  struct Instance {
    float x;
    float y;
    float z;
    float rotY;
    // Surface up-normal the model is tilted onto (default +Y = upright).
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
  };

  EntityRenderer() = default;
  ~EntityRenderer();

  EntityRenderer(const EntityRenderer&)            = delete;
  EntityRenderer& operator=(const EntityRenderer&) = delete;

  // Allocates VAOs / VBOs / EBOs. Requires a current GL context.
  void initGL();

  // Snap-on-tick variants: take NPC/item snapshots, look up tile heights,
  // and upload integer-tile positions. Kept for back-compat / when no
  // interpolation context is available.
  void rebuildNpcs (const std::vector<shared::NPCState>&         npcs,
                    const shared::WorldMapFile&                  map);
  void rebuildItems(const std::vector<shared::DroppedItemState>& items,
                    const shared::WorldMapFile&                  map);

  // Direct upload of pre-built instance arrays — used by App when it has
  // interpolated tween data between prev/curr state snapshots.
  // `kinds` is parallel to `insts` — the NPC kind string for each instance.
  // Instances whose kind has a loaded custom model are drawn with that model;
  // the rest fall back to the humanoid procedural geometry.
  void setNpcInstances (const std::vector<Instance>& insts,
                        const std::vector<std::string>& kinds = {});
  void setItemInstances(const std::vector<Instance>& insts);

  // Resolver maps a relative model_path → absolute path; primes the NPC
  // ModelLibrary with the NPC placeholder. Call once after initGL().
  void setNpcModelResolver(std::function<std::filesystem::path(const std::string&)> r) {
    npcResolver_ = r;
    if (!npcModelsInited_) {
      npcModels_.init(r, "assets/models/_placeholder_npc.gltf");
      npcModelsInited_ = true;
    }
  }
  // Load/cache a per-kind NPC model (or placeholder when modelPath is empty).
  void ensureNpcModel(const std::string& kind, const std::string& modelPath,
                      int sizeX = 1, int sizeY = 1) {
    npcModels_.ensure(kind, modelPath, sizeX, sizeY);
    if (npcModels_.isAnimated(kind)) anyNpcAnimated_ = true;
  }
  // World-space AABB for an NPC kind's model + footprint (for picking).
  bool npcAabb(const std::string& kind, glm::vec3& mn, glm::vec3& mx) const {
    return const_cast<ModelLibrary&>(npcModels_).aabb(kind, mn, mx);
  }

  // True if any loaded NPC kind is animated (host gates the skinned pass on it).
  bool hasAnimatedNpcs() const { return anyNpcAnimated_; }

  // ---- Dropped-item models (DB-driven via model_dropped) -------------------
  // Items WITH a model_dropped render that model; items without keep the small
  // placeholder box (itemBox_).
  void setItemModelResolver(std::function<std::filesystem::path(const std::string&)> r) {
    itemResolver_ = r;
    if (!itemModelsInited_) { itemModels_.init(r, ""); itemModelsInited_ = true; }
  }
  // Register a model for an item id. No-op for an empty path (those items keep
  // the placeholder box rather than a giant unit-cube placeholder).
  void ensureItemModel(const std::string& itemId, const std::string& modelPath,
                       int sizeX = 1, int sizeY = 1) {
    if (modelPath.empty()) return;
    itemModels_.ensure(itemId, modelPath, sizeX, sizeY);
    if (itemModels_.isAnimated(itemId)) anyItemAnimated_ = true;
  }
  bool itemHasModel(const std::string& itemId) const {
    return const_cast<ModelLibrary&>(itemModels_).has(itemId);
  }
  // Editor hot-reload: reload NPC + item models whose source files changed.
  bool reloadModelsIfChanged() {
    const bool a = npcModels_.reloadIfChanged();
    const bool b = itemModels_.reloadIfChanged();
    return a || b;
  }
  bool itemAabb(const std::string& itemId, glm::vec3& mn, glm::vec3& mx) const {
    return const_cast<ModelLibrary&>(itemModels_).aabb(itemId, mn, mx);
  }
  // Narrow-phase ray-mesh pick (1 = hit/writes t, 0 = missed, -1 = no geom).
  int itemRayHit(const std::string& itemId, const glm::mat4& world,
                 const glm::vec3& ro, const glm::vec3& rd, float& t) const {
    return itemModels_.rayHitWorld(itemId, world, ro, rd, t);
  }
  int npcRayHit(const std::string& kind, const glm::mat4& world,
                const glm::vec3& ro, const glm::vec3& rd, float& t) const {
    return npcModels_.rayHitWorld(kind, world, ro, rd, t);
  }
  bool hasAnimatedItems() const { return anyItemAnimated_; }
  // Draw animated item models (skinned shader); advances clips once per frame.
  void renderAnimatedItems(render::Shader& skinnedShader, float dt);

  // Animated NPCs into the skinned shadow depth pass (no clip advance).
  void renderNpcAnimatedShadows(render::Shader& skinnedDepthShader);

  // Draw animated NPC kinds via the skinned shader, one draw per instance.
  // The caller has already set the skinned shader's lighting uniforms (same
  // setup as the fishing-spot / custom-object pass). Advances each kind's clip
  // once per frame. Instances + smoothed yaw come from the last
  // setNpcInstances() call.
  void renderAnimatedNpcs(render::Shader& skinnedShader, float dt);

  // One instanced draw per kind, in the order NPCs then items.
  // The caller has already set u_viewProj / u_lightDir / palette uniforms;
  // we set u_color per draw.
  void render(render::Shader& shader);

  // Depth-only pass for shadow map generation. Caller sets u_lightViewProj;
  // no u_color needed. Uses the same NPC VAOs as render().
  void renderDepth(render::Shader& shader);

  // Two-pass stencil outline for a single hovered NPC or dropped item.
  // Sets all required uniforms on `outlineShader` and restores GL state.
  void renderNpcOutline (render::Shader& outlineShader,
                         const glm::mat4& viewProj,
                         const Instance& inst,
                         const glm::vec4& color) const;
  void renderItemOutline(render::Shader& outlineShader,
                         const glm::mat4& viewProj,
                         const Instance& inst,
                         const glm::vec4& color) const;

  // Render just the geometry for a single NPC or dropped item (no stencil,
  // no inflation). u_viewProj must already be set on the shader. Used by the
  // screen-space outline mask pass to build a silhouette texture.
  // `kind` selects the custom model if one was loaded for that NPC kind;
  // falls back to the humanoid procedural geometry when empty or unknown.
  void renderNpcGeometry (render::Shader& maskShader, const Instance& inst,
                          const std::string& kind = "") const;
  void renderItemGeometry(render::Shader& maskShader, const Instance& inst,
                          const std::string& itemId = "") const;

  std::size_t npcCount()  const { return npcCount_;  }
  std::size_t itemCount() const { return itemCount_; }

  // Read-only access to the CPU-side NPC instance data (position + smoothed
  // rotY) and their kind strings, parallel arrays. Used by App to look up the
  // exact interpolated transform for the outline pass.
  const std::vector<Instance>&    npcInstsCpu()  const { return npcInstCpu_; }
  const std::vector<std::string>& npcKindsCpu()  const { return npcKinds_;  }

private:
  // Maximum instances uploadable per kit (matches the cap in the .cpp).
  static constexpr std::size_t kInstanceCap = 1024;

  struct Kit {
    GLuint    vao        = 0;
    GLuint    vboPos     = 0;
    GLuint    vboNrm     = 0;
    GLuint    ebo        = 0;
    GLsizei   indexCount = 0;
    glm::vec3 color      = glm::vec3(1.0f);
  };

  void destroy();
  void uploadKit(Kit& dst,
                 const std::vector<float>&    positions,
                 const std::vector<float>&    normals,
                 const std::vector<uint32_t>& indices,
                 GLuint instanceVbo);

  // NPC models are fully data-driven via the shared ModelLibrary (model file or
  // placeholder, static or animated). Dropped items keep a procedural box.
  ModelLibrary npcModels_;
  std::function<std::filesystem::path(const std::string&)> npcResolver_;
  bool npcModelsInited_ = false;
  bool anyNpcAnimated_  = false;

  Kit itemBox_;

  // Dropped items with a model_dropped are data-driven via the shared
  // ModelLibrary, keyed by item id. Items without a model use itemBox_.
  ModelLibrary itemModels_;
  std::function<std::filesystem::path(const std::string&)> itemResolver_;
  bool itemModelsInited_ = false;
  bool anyItemAnimated_  = false;
  std::unordered_map<std::string, std::vector<ModelLibrary::Instance>> itemModelGroups_;

  GLuint itemInstanceVbo_       = 0;
  std::size_t npcCount_         = 0;
  std::size_t itemCount_        = 0;

  // CPU-side copies kept so render() can group by kind without a GPU readback.
  std::vector<Instance>     npcInstCpu_;
  std::vector<std::string>  npcKinds_;

  // Single-instance VBO + dedicated VAO used for the 2-pass stencil outline of
  // dropped items.
  GLuint outlineInstanceVbo_ = 0;
  GLuint itemOutlineVao_     = 0;

  // Internal helper: build an outline VAO sharing geometry from `kit` but
  // driven by outlineInstanceVbo_ instead of the kit's normal instance VBO.
  GLuint buildOutlineVao(const Kit& kit) const;
  // Internal helper: execute the two-pass stencil outline on `vao`.
  void   doOutline2Pass(render::Shader& shader, GLuint vao, GLsizei indexCount,
                        const glm::mat4& viewProj,
                        const Instance& inst,
                        const glm::vec4& color,
                        float outlineWidth) const;
};

}  // namespace world
