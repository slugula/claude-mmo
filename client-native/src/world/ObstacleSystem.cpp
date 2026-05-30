#include "world/ObstacleSystem.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace world {

namespace {
// Average of a tile's 4 corner vertex heights → tile-centre world Y.
float tileCenterY(const std::vector<float>& vh, int W, int H, int tx, int ty) {
  const float SW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float SE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  const float NW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float NE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}
}  // namespace

// ---- Definitions ----------------------------------------------------------

void ObstacleSystem::seedBuiltinDefinitions() {
  // Built-in object types are seeded with empty model paths — they render the
  // placeholder until real meshes are supplied via the DB editor.
  auto seed = [&](const char* id, const char* type, const char* col, int sx, int sy) {
    ObjectDefCache d;
    d.id = id; d.objectType = type; d.collision = col; d.sizeX = sx; d.sizeY = sy;
    defs_.emplace(id, d);
  };
  seed("tree",         "ResourceNode", "full_blocking", 1, 1);
  seed("rock",         "ResourceNode", "full_blocking", 1, 1);
  seed("chest",        "Decoration",   "full_blocking", 1, 1);
  seed("fishing_spot", "ResourceNode", "none",          1, 1);
  seed("fence",        "Decoration",   "half_blocking", 1, 1);
}

void ObstacleSystem::rebuildFromDefinitions(const std::vector<ObjectDefCache>& defs) {
  seedBuiltinDefinitions();
  for (const auto& d : defs)
    if (!d.id.empty()) defs_[d.id] = d;
  loadCustomModels();
}

const ObstacleSystem::ObjectDefCache* ObstacleSystem::getDefinition(const std::string& id) const {
  const auto it = defs_.find(id);
  return (it != defs_.end()) ? &it->second : nullptr;
}

std::pair<int,int> ObstacleSystem::footprint(const std::string& id) const {
  const auto* d = getDefinition(id);
  return d ? std::make_pair(d->sizeX, d->sizeY) : std::make_pair(1, 1);
}

void ObstacleSystem::loadCustomModels() {
  models_.clearEntries();
  if (!modelResolver_) return;
  // Register every object definition. fishing_spot is excluded — it has its own
  // dedicated animated render path in the host.
  for (const auto& [id, def] : defs_) {
    if (id == "fishing_spot") continue;
    models_.ensure(id, def.modelPath, def.sizeX, def.sizeY);
  }
}

// ---- Instances ------------------------------------------------------------

void ObstacleSystem::rebuildFromMap(const shared::WorldMapFile& map) {
  customInstances_.clear();

  const int W = map.width, H = map.height;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return;

  for (int ty = 0; ty < H; ++ty) {
    for (int tx = 0; tx < W; ++tx) {
      const auto& tile = map.tiles[ty][tx];
      if (tile.obstacle.empty() || tile.obstacle == "none") continue;
      if (tile.obstacle == "fishing_spot") continue;   // host renders these
      if (!models_.has(tile.obstacle)) continue;

      const float y = tileCenterY(vh, W, H, tx, ty);
      customInstances_[tile.obstacle].push_back(
          ModelLibrary::Instance{ static_cast<float>(tx), y,
                                  static_cast<float>(ty), 0.0f });  // orientation in model
    }
  }
}

// ---- Rendering ------------------------------------------------------------

void ObstacleSystem::render(render::Shader& obstacleShader) {
  for (auto& [id, insts] : customInstances_) {
    if (models_.isAnimated(id)) continue;
    models_.drawStaticInstanced(obstacleShader, id, insts);
  }
}

void ObstacleSystem::renderCustomAnimated(render::Shader& skinnedShader, float dt) {
  models_.update(dt);  // advance all animated clips once per frame
  for (auto& [id, insts] : customInstances_) {
    if (!models_.isAnimated(id)) continue;
    for (const ModelLibrary::Instance& in : insts) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(in.x, in.y, in.z));
      models_.drawAnimatedAt(skinnedShader, id, m);
    }
  }
}

void ObstacleSystem::renderDepth(render::Shader& depthShader) {
  for (auto& [id, insts] : customInstances_) {
    if (models_.isAnimated(id)) continue;
    models_.drawStaticInstanced(depthShader, id, insts);
  }
}

void ObstacleSystem::renderAnimatedShadows(render::Shader& skinnedDepthShader) {
  for (auto& [id, insts] : customInstances_) {
    if (!models_.isAnimated(id)) continue;
    for (const ModelLibrary::Instance& in : insts) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(in.x, in.y, in.z));
      models_.drawAnimatedAt(skinnedDepthShader, id, m);
    }
  }
}

bool ObstacleSystem::renderGeometryAt(render::Shader& maskShader,
                                      const shared::WorldMapFile& map,
                                      int tileX, int tileY) {
  if (tileY < 0 || tileY >= map.height || tileX < 0 || tileX >= map.width) return false;
  const auto& obs = map.tiles[tileY][tileX].obstacle;
  if (obs.empty() || obs == "fishing_spot" || !models_.has(obs)) return false;
  if (models_.isAnimated(obs)) return false;   // no skinned mask shader yet

  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (map.width + 1) * (map.height + 1)) return false;
  const float cy = tileCenterY(vh, map.width, map.height, tileX, tileY);

  glDisable(GL_STENCIL_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  models_.drawStaticInstanced(maskShader, obs,
      { ModelLibrary::Instance{ static_cast<float>(tileX), cy,
                                static_cast<float>(tileY), 0.0f } });
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  return true;
}

}  // namespace world
