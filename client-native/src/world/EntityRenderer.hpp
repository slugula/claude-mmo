#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "world/GltfModel.hpp"
#include "world/SkinnedMesh.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
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

  // Load / clear per-kind custom model geometry.
  // Call at startup after fetching NPC definitions from the DB API.
  // `primitives` and `materials` come from world::loadGlb().
  void loadNpcKindModel(const std::string&                kind,
                        const std::vector<GltfPrimitive>& primitives,
                        const std::vector<GltfMaterial>&  materials);

  // Load a per-kind NPC model from a glTF file. If the model contains
  // animation clips it is stored as a SkinnedMesh and drawn (animated, looping
  // its first clip) via renderAnimatedNpcs(); otherwise it is uploaded as
  // static instanced geometry (same as the primitives overload above).
  void loadNpcKindModel(const std::string& kind, const std::filesystem::path& path);

  void clearNpcKindModels();

  // True if any loaded NPC kind is animated (host gates the skinned pass on it).
  bool hasAnimatedNpcs() const { return !npcKindSkinned_.empty(); }

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
  void renderItemGeometry(render::Shader& maskShader, const Instance& inst) const;

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

  // One primitive of a custom (DB-loaded) NPC model.
  struct CustomPrim {
    GLuint  vao        = 0;
    GLuint  vboPos     = 0;
    GLuint  vboNrm     = 0;
    GLuint  ebo        = 0;
    GLsizei indexCount = 0;
    glm::vec4 color    = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
    // Separate VAO wired to outlineInstanceVbo_ for the mask/geometry pass.
    // Shares the same geometry buffers as `vao`.
    GLuint  outlineVao = 0;
  };
  struct CustomKit { std::vector<CustomPrim> prims; };

  void destroy();
  void uploadKit(Kit& dst,
                 const std::vector<float>&    positions,
                 const std::vector<float>&    normals,
                 const std::vector<uint32_t>& indices,
                 GLuint instanceVbo);

  Kit humanoid_;
  Kit itemBox_;

  GLuint npcInstanceVbo_        = 0;
  GLuint itemInstanceVbo_       = 0;
  GLuint customNpcInstanceVbo_  = 0;   // scratch VBO for per-kind instanced draws
  std::size_t npcCount_         = 0;
  std::size_t itemCount_        = 0;

  // CPU-side copies kept so render() can group by kind without a GPU readback.
  std::vector<Instance>     npcInstCpu_;
  std::vector<std::string>  npcKinds_;

  std::unordered_map<std::string, CustomKit> npcKindKits_;

  // Animated NPC kinds — rendered per-instance via the skinned shader.
  std::unordered_map<std::string, std::unique_ptr<world::SkinnedMesh>> npcKindSkinned_;
  bool isAnimatedKind(const std::string& k) const {
    return !k.empty() && npcKindSkinned_.count(k) > 0;
  }

  // Single-instance VBO + dedicated VAOs used for the 2-pass stencil outline.
  // Separate from the batched instance VBOs so we can upload one instance
  // without corrupting the multi-instance draw data.
  GLuint outlineInstanceVbo_ = 0;
  GLuint npcOutlineVao_      = 0;
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
