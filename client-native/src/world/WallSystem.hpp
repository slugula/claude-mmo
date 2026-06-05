#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>

#include <vector>

namespace world {

// Renders wall + pillar edge features (map.walls) as placeholder geometry:
//   - cardinal walls  (orient 0/2/4/6) hug a tile edge,
//   - diagonal walls  (orient 1/3/5/7) span the tile corner-to-corner,
//   - pillars sit at a tile corner.
// Each is a thin tall box drawn instanced through the obstacle shader (pos +
// Y-rotation per instance). Real meshes (DB-uploaded) come in a later phase.
class WallSystem {
public:
  WallSystem() = default;
  ~WallSystem();

  WallSystem(const WallSystem&)            = delete;
  WallSystem& operator=(const WallSystem&) = delete;

  void initGL();    // build the three procedural kits (needs a GL context)
  void destroy();

  // Gather instances from the map's wall list. Cheap; call when the map changes.
  void rebuildFromMap(const shared::WorldMapFile& map);

  // Draw all walls/pillars via the obstacle shader (caller sets the common
  // uniforms; this sets u_color per kit).
  void render(render::Shader& obstacleShader);

  bool empty() const {
    return cardinal_.insts.empty() && diagonal_.insts.empty() && pillar_.insts.empty();
  }

private:
  struct Instance { float x, y, z, rotY; };
  struct Kit {
    GLuint  vao = 0, vboPos = 0, vboNrm = 0, vboCol = 0, ebo = 0, instVbo = 0;
    GLsizei indexCount = 0;
    GLsizei instCap    = 0;       // allocated instance capacity
    glm::vec3 color    = glm::vec3(0.6f);
    std::vector<Instance> insts;
  };

  void buildKit(Kit& k, const std::vector<float>& pos, const std::vector<float>& nrm,
                const std::vector<uint32_t>& idx, glm::vec3 color);
  void uploadInstances(Kit& k);
  void drawKit(render::Shader& shader, Kit& k);
  void destroyKit(Kit& k);

  Kit cardinal_;   // edge wall
  Kit diagonal_;   // corner-to-corner wall
  Kit pillar_;     // corner column
};

}  // namespace world
