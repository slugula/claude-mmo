#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "world/ModelLibrary.hpp"

#include <glad/glad.h>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

  // Wire the model library so wall/pillar variants with an uploaded mesh render
  // their glTF instead of the placeholder. Call once after initGL().
  void setModelResolver(std::function<std::filesystem::path(const std::string&)> r);
  // Register variant id → model path (empty path → placeholder). Loads meshes.
  void setWallDefs(const std::vector<std::pair<std::string, std::string>>& idToModel);

  // Gather instances from the map's wall list. Cheap; call when the map changes.
  void rebuildFromMap(const shared::WorldMapFile& map);

  // Draw all walls/pillars via the obstacle shader (caller sets the common
  // uniforms; this sets u_color per kit).
  void render(render::Shader& obstacleShader);

  // Editor placement preview: draw one wall/pillar at a tile with the given
  // orientation (caller wraps it in translucent blend state).
  void renderGhostAt(render::Shader& obstacleShader, const shared::WorldMapFile& map,
                     int tileX, int tileY, int orient, bool pillar,
                     const std::string& objectId = {});

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

  Kit cardinal_;   // edge wall  (placeholder)
  Kit diagonal_;   // corner-to-corner wall (placeholder)
  Kit pillar_;     // corner column (placeholder)
  GLuint ghostVbo_ = 0;   // single-instance buffer for the placement preview

  // Uploaded variant meshes (objectId → glTF). Walls whose objectId has a real
  // mesh render it; the rest fall back to the procedural placeholder kits.
  ModelLibrary                        meshes_;
  bool                                meshesInited_ = false;
  std::unordered_set<std::string>     meshIds_;     // ids that have a real mesh
  std::unordered_map<std::string, std::vector<ModelLibrary::Instance>> meshInsts_;
};

}  // namespace world
