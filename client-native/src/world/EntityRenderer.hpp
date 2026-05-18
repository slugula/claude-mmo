#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

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
  void setNpcInstances (const std::vector<Instance>& insts);
  void setItemInstances(const std::vector<Instance>& insts);

  // One instanced draw per kind, in the order NPCs then items.
  // The caller has already set u_viewProj / u_lightDir / palette uniforms;
  // we set u_color per draw.
  void render(render::Shader& shader);

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

  std::size_t npcCount()  const { return npcCount_;  }
  std::size_t itemCount() const { return itemCount_; }

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

  Kit humanoid_;
  Kit itemBox_;

  GLuint npcInstanceVbo_   = 0;
  GLuint itemInstanceVbo_  = 0;
  std::size_t npcCount_    = 0;
  std::size_t itemCount_   = 0;

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
