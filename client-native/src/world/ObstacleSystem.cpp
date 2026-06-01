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
  // Register every object definition with the shared model library. Animated
  // models (e.g. fishing_spot) are auto-detected and routed to the skinned path.
  for (const auto& [id, def] : defs_) {
    models_.ensure(id, def.modelPath, def.sizeX, def.sizeY);
  }
}

// Effective object id to render/pick for a tile: when depleted, the source
// object's `depletedObjectId` (another normal object, e.g. a stump); otherwise
// the tile's own obstacle id. Returns "" if depleted with no referenced object.
std::string ObstacleSystem::effectiveId(const std::string& obs, bool depleted) const {
  if (!depleted) return obs;
  const auto it = defs_.find(obs);
  if (it == defs_.end()) return "";
  return it->second.depletedObjectId;   // empty = render nothing while depleted
}

bool ObstacleSystem::isPickable(const std::string& id) const {
  const auto it = defs_.find(id);
  return it == defs_.end() ? true : it->second.pickable;
}

// ---- Instances ------------------------------------------------------------

void ObstacleSystem::rebuildFromMap(const shared::WorldMapFile& map,
                                   const std::unordered_set<std::string>& depletedKeys) {
  customInstances_.clear();

  const int W = map.width, H = map.height;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return;

  for (int ty = 0; ty < H; ++ty) {
    for (int tx = 0; tx < W; ++tx) {
      const auto& tile = map.tiles[ty][tx];
      if (tile.obstacle.empty() || tile.obstacle == "none") continue;

      // Depleted resource nodes render their referenced depleted object (or
      // nothing if the definition references none).
      const bool depleted = !depletedKeys.empty() &&
          depletedKeys.count(std::to_string(tx) + "-" + std::to_string(ty)) > 0;
      const std::string modelId = effectiveId(tile.obstacle, depleted);
      if (modelId.empty() || !models_.has(modelId)) continue;

      const float y = tileCenterY(vh, W, H, tx, ty);
      const float rotY = static_cast<float>(tile.obstacleRotation) * 1.57079632679f; // n*90°
      customInstances_[modelId].push_back(
          ModelLibrary::Instance{ static_cast<float>(tx), y,
                                  static_cast<float>(ty), rotY });
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
                                      render::Shader& maskSkinnedShader,
                                      const shared::WorldMapFile& map,
                                      int tileX, int tileY,
                                      const std::unordered_set<std::string>& depletedKeys) {
  if (tileY < 0 || tileY >= map.height || tileX < 0 || tileX >= map.width) return false;
  const auto& obs = map.tiles[tileY][tileX].obstacle;
  if (obs.empty()) return false;

  const bool depleted = depletedKeys.count(std::to_string(tileX) + "-" + std::to_string(tileY)) > 0;
  const std::string id = effectiveId(obs, depleted);
  if (id.empty() || !models_.has(id) || !isPickable(id)) return false;

  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (map.width + 1) * (map.height + 1)) return false;
  const float cy = tileCenterY(vh, map.width, map.height, tileX, tileY);
  const float rotY = static_cast<float>(map.tiles[tileY][tileX].obstacleRotation) * 1.57079632679f;
  glm::mat4 m = glm::translate(glm::mat4(1.0f),
      glm::vec3(static_cast<float>(tileX), cy, static_cast<float>(tileY)));
  m = glm::rotate(m, rotY, glm::vec3(0.0f, 1.0f, 0.0f));

  glDisable(GL_STENCIL_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  if (models_.isAnimated(id)) {
    // SkinnedMesh::render() issues raw glUniform* against the ACTIVE program, so
    // the skinned mask program must be bound first (the host leaves the static
    // mask program active). Restore it afterwards for subsequent NPC/item draws.
    maskSkinnedShader.use();
    models_.drawAnimatedAt(maskSkinnedShader, id, m);
    maskShader.use();
  } else {
    maskShader.use();
    models_.drawStaticInstanced(maskShader, id,
        { ModelLibrary::Instance{ static_cast<float>(tileX), cy,
                                  static_cast<float>(tileY), rotY } });
  }
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  return true;
}

bool ObstacleSystem::renderGhostAt(render::Shader& obstacleShader,
                                   render::Shader& skinnedShader,
                                   const shared::WorldMapFile& map,
                                   const std::string& id,
                                   int tileX, int tileY, int rotationQuarter) {
  if (id.empty() || !models_.has(id)) return false;
  if (tileY < 0 || tileY >= map.height || tileX < 0 || tileX >= map.width) return false;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (map.width + 1) * (map.height + 1)) return false;

  const float cy   = tileCenterY(vh, map.width, map.height, tileX, tileY);
  const float rotY = static_cast<float>(rotationQuarter & 3) * 1.57079632679f;

  if (models_.isAnimated(id)) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f),
        glm::vec3(static_cast<float>(tileX), cy, static_cast<float>(tileY)));
    m = glm::rotate(m, rotY, glm::vec3(0.0f, 1.0f, 0.0f));
    skinnedShader.use();
    models_.drawAnimatedAt(skinnedShader, id, m);
    obstacleShader.use();
  } else {
    obstacleShader.use();
    models_.drawStaticInstanced(obstacleShader, id,
        { ModelLibrary::Instance{ static_cast<float>(tileX), cy,
                                  static_cast<float>(tileY), rotY } });
  }
  return true;
}

}  // namespace world
