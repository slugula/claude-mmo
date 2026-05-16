#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace world {

// Manages the GPU resources for rendering trees and rocks: three procedural
// "kit" meshes (trunk cylinder, canopy sphere, rock box) plus two per-
// instance VBOs (one for trees, one for rocks). One instanced draw per mesh
// kit each frame. Rebuilt whenever the map regenerates.
class ObstacleSystem {
public:
  ObstacleSystem() = default;
  ~ObstacleSystem();

  ObstacleSystem(const ObstacleSystem&)            = delete;
  ObstacleSystem& operator=(const ObstacleSystem&) = delete;

  // One-time GL setup — uploads the three static kit meshes. Call after the
  // GL context is current.
  void initGL();

  // Walk the map's tiles, collect tree and rock instances (position +
  // rotation), and re-upload the instance VBOs. Cheap — runs in microseconds
  // for a 64x64 map.
  void rebuildFromMap(const shared::WorldMapFile& map);

  // Issue three instanced draws (trunks, canopies, rocks). The shader is
  // expected to have all uniforms except u_color set by the caller; this
  // method sets u_color per draw to match the obstacle kit being drawn.
  void render(render::Shader& obstacleShader);

  std::size_t treeCount() const { return treeCount_; }
  std::size_t rockCount() const { return rockCount_; }

private:
  // Per-kit static-geometry handles + per-kit instance VAO that combines
  // them with the shared instance buffer for that obstacle kind.
  struct Kit {
    GLuint  vboPositions = 0;  // 3 floats per vertex
    GLuint  vboNormals   = 0;  // 3 floats per vertex
    GLuint  ebo          = 0;  // uint32 indices
    GLuint  vao          = 0;  // attribute layout (mesh + instance binding)
    GLsizei indexCount   = 0;
    glm::vec3 color      = glm::vec3(1.0f);
  };

  void destroy();
  void uploadKitMesh(Kit& kit,
                     const std::vector<float>&    positions,
                     const std::vector<float>&    normals,
                     const std::vector<uint32_t>& indices,
                     GLuint instanceVbo);

  Kit trunk_;
  Kit canopy_;
  Kit rock_;

  GLuint treeInstanceVbo_ = 0;  // shared between trunk and canopy VAOs
  GLuint rockInstanceVbo_ = 0;

  std::size_t treeCount_ = 0;
  std::size_t rockCount_ = 0;
};

}  // namespace world
