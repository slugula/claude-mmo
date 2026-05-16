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
// Movement is snapped per-tick (no interpolation between state snapshots);
// Phase 10 polish will add smoothing when this becomes visible.
class EntityRenderer {
public:
  EntityRenderer() = default;
  ~EntityRenderer();

  EntityRenderer(const EntityRenderer&)            = delete;
  EntityRenderer& operator=(const EntityRenderer&) = delete;

  // Allocates VAOs / VBOs / EBOs. Requires a current GL context.
  void initGL();

  // Replace the per-instance buffer for NPCs from the latest state. Skips
  // entries whose tile is outside the map bounds. Caller passes the current
  // procedural map so we can sample tile elevations.
  void rebuildNpcs (const std::vector<shared::NPCState>&         npcs,
                    const shared::WorldMapFile&                  map);
  void rebuildItems(const std::vector<shared::DroppedItemState>& items,
                    const shared::WorldMapFile&                  map);

  // One instanced draw per kind, in the order NPCs then items.
  // The caller has already set u_viewProj / u_lightDir / palette uniforms;
  // we set u_color per draw.
  void render(render::Shader& shader);

  std::size_t npcCount()  const { return npcCount_;  }
  std::size_t itemCount() const { return itemCount_; }

private:
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

  GLuint npcInstanceVbo_  = 0;
  GLuint itemInstanceVbo_ = 0;
  std::size_t npcCount_   = 0;
  std::size_t itemCount_  = 0;
};

}  // namespace world
