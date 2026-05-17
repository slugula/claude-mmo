#pragma once

#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace world {

struct WaterVertex {
  glm::vec3 pos;
  glm::vec2 uv;
  glm::vec3 normal;
  float     shore_weight;  // 0 = open water, 1 = shoreline
};

// Builds a flat quad mesh for all water tiles in a WorldMapFile.
// Each WaterTile gets one quad with per-vertex shore_weight derived from
// neighbouring tiles.  Water Y per tile = avg non-water-neighbour terrain
// height - waterOffset.
class WaterMesh {
public:
  WaterMesh()  = default;
  ~WaterMesh() { destroy(); }

  WaterMesh(const WaterMesh&)            = delete;
  WaterMesh& operator=(const WaterMesh&) = delete;

  // Rebuild from the current map state.  waterOffset is in world units.
  void build(const shared::WorldMapFile& map, float waterOffset);

  void destroy();
  void draw()  const;
  bool empty() const { return indexCount_ == 0; }

private:
  GLuint vao_        = 0;
  GLuint vbo_        = 0;
  GLuint ebo_        = 0;
  int    indexCount_ = 0;
};

}  // namespace world
