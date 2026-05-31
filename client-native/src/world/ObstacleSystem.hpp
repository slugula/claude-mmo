#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "world/ModelLibrary.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world {

// Renders all placed objects from the map, fully data-driven. Every object id
// is backed by the shared ModelLibrary (a model file, or a placeholder when the
// definition has no model). One instanced static draw per id, plus a skinned
// pass for animated ids. No procedural/built-in meshes — built-ins are just
// pre-seeded definitions that render their model (or the placeholder).
class ObstacleSystem {
public:
  ObstacleSystem() = default;
  ~ObstacleSystem() = default;

  ObstacleSystem(const ObstacleSystem&)            = delete;
  ObstacleSystem& operator=(const ObstacleSystem&) = delete;

  void initGL() {}   // nothing to allocate up front; ModelLibrary owns GL

  // Gather per-id instance lists from the map tiles (one instance per placed
  // object, at its anchor tile). Cheap; runs each time the map changes.
  // `depletedKeys` holds "x-y" keys of resource nodes the server reports as
  // depleted; those tiles render the object's depleted-model variant (id +
  // "#depleted") instead, or nothing if it has none.
  void rebuildFromMap(const shared::WorldMapFile& map,
                      const std::unordered_set<std::string>& depletedKeys = {});

  // Database-authored object definition (collision, footprint, model, action…).
  struct ObjectDefCache {
    std::string id;
    std::string objectType   = "Decoration";
    std::string collision    = "full_blocking";
    int         sizeX        = 1;
    int         sizeY        = 1;
    std::string modelPath;
    std::string actionId;
    std::string dropItemId;
    int         dropQuantity  = 1;
    int         respawnTicks  = 25;
    std::string defaultClip;
    bool        looping       = true;
    float       rotationX     = 0.f;
    float       rotationY     = 0.f;
    float       rotationZ     = 0.f;
    std::string depletedModel;
  };

  // Seed built-in defaults, overlay server definitions, then (re)load all
  // models into the ModelLibrary.
  void rebuildFromDefinitions(const std::vector<ObjectDefCache>& defs);
  void seedBuiltinDefinitions();
  const ObjectDefCache* getDefinition(const std::string& id) const;
  std::pair<int,int> footprint(const std::string& id) const;

  // Resolver maps a relative model_path → absolute path; sets up the
  // ModelLibrary with the object placeholder. Call once after initGL().
  void setModelResolver(std::function<std::filesystem::path(const std::string&)> r) {
    modelResolver_ = r;
    if (!modelsInited_) {
      models_.init(r, "assets/models/_placeholder_object.gltf");
      modelsInited_ = true;
    }
  }

  // Static (non-animated) objects — instanced draw via the obstacle shader.
  void render(render::Shader& obstacleShader);
  // Animated objects — skinned draw, advances clips by dt once.
  void renderCustomAnimated(render::Shader& skinnedShader, float dt);
  // Static objects into the instanced shadow depth pass.
  void renderDepth(render::Shader& depthShader);
  // Animated objects into the skinned shadow depth pass (no clip advance).
  void renderAnimatedShadows(render::Shader& skinnedDepthShader);

  bool hasCustomModels() const { return !customInstances_.empty(); }

  // Render one object's model into the outline mask (static only).
  bool renderGeometryAt(render::Shader& maskShader,
                        const shared::WorldMapFile& map,
                        int tileX, int tileY);

  // World-space AABB for an object id (model bounds ∪ footprint), in model
  // space centred on its tile. Returns false for unknown ids.
  bool customAabb(const std::string& id, glm::vec3& outMin, glm::vec3& outMax) const {
    return const_cast<ModelLibrary&>(models_).aabb(id, outMin, outMax);
  }

private:
  void loadCustomModels();   // ensure a ModelLibrary entry for every definition

  ModelLibrary models_;
  std::unordered_map<std::string, std::vector<ModelLibrary::Instance>> customInstances_;
  std::function<std::filesystem::path(const std::string&)> modelResolver_;
  bool modelsInited_ = false;

  std::unordered_map<std::string, ObjectDefCache> defs_;
};

}  // namespace world
