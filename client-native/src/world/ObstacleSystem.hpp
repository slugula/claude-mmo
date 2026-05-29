#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
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

  // Load a glTF tree model to replace the procedural trunk+canopy. The model
  // geometry is pre-scaled to fill a 2×2-tile footprint. Non-fatal on failure
  // (falls back to the procedural kit). Should be called after initGL().
  bool loadTreeModel(const std::filesystem::path& path);

  // Walk the map's tiles, collect tree and rock instances (position +
  // rotation), and re-upload the instance VBOs. Cheap — runs in microseconds
  // for a 64x64 map.
  void rebuildFromMap(const shared::WorldMapFile& map);

  // Lightweight cache of database-authored object definitions. Stores
  // collision, sizeX, sizeY (and model_path for future model loading) keyed by
  // object id ("tree", "bookcase_oak", etc.). Built-in enum types are seeded
  // automatically; custom ids can coexist. The collision and size values are
  // available for picking, pathfinding, and future data-driven rendering.
  struct ObjectDefCache {
    std::string id;                           // "tree", "bookcase_oak", etc.
    std::string objectType   = "Decoration";  // "Decoration"|"ResourceNode"|"ProductionFacility"
    std::string collision    = "full_blocking";
    int         sizeX        = 1;
    int         sizeY        = 1;
    std::string modelPath;
    std::string actionId;
    std::string dropItemId;
    int         dropQuantity  = 1;
    int         respawnTicks  = 25;
    // Animation & orientation (from ObjectDef)
    std::string defaultClip;           // glTF clip name; empty = first clip
    bool        looping       = true;
    float       rotationX     = 0.f;   // degrees
    float       rotationY     = 0.f;
    float       rotationZ     = 0.f;
  };

  // Populate the definitions cache from the server DB. Each entry's `id` field
  // is the map key. The five built-in types (tree, rock, chest, fishing_spot,
  // fence) are seeded with hardcoded defaults before overlaying the supplied
  // defs, so they always have a fallback. Safe to call every time the DB
  // editor refreshes.
  void rebuildFromDefinitions(const std::vector<ObjectDefCache>& defs);

  // Seed built-in definitions so the cache works without a server connection.
  void seedBuiltinDefinitions();

  // Look up a definition by object id. Returns nullptr if not found.
  const ObjectDefCache* getDefinition(const std::string& id) const;

  // Convenience: return sizeX × sizeY footprint for the given id.
  // Returns {1,1} for unknown ids.
  std::pair<int,int> footprint(const std::string& id) const;

  // Issue instanced draws for all obstacles (single pass — all tiles,
  // above and below water). The shader must have all uniforms except u_color
  // set by the caller; this method sets u_color per draw.
  void render(render::Shader& obstacleShader);

  // Depth-only pass for shadow casting.
  void renderDepth(render::Shader& depthShader);

  // Render an outline around a single obstacle at the given tile.
  bool renderOutlineAt(render::Shader& outlineShader,
                       const shared::WorldMapFile& map,
                       int tileX, int tileY);

  // Render just the geometry for a single obstacle (no stencil / inflation).
  bool renderGeometryAt(render::Shader& maskShader,
                        const shared::WorldMapFile& map,
                        int tileX, int tileY);

  std::size_t treeCount()  const { return treeCount_;  }
  std::size_t rockCount()  const { return rockCount_;  }
  std::size_t fenceCount() const { return fenceCount_; }

  bool        treeModelLoaded()   const { return treeModelLoaded_; }
  glm::vec3   treeGltfAABBMin()   const { return treeGltfAABBMin_; }
  glm::vec3   treeGltfAABBMax()   const { return treeGltfAABBMax_; }

private:
  struct Kit {
    GLuint  vboPositions = 0;
    GLuint  vboNormals   = 0;
    GLuint  ebo          = 0;
    GLuint  vao          = 0;
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
  Kit fence_;

  GLuint treeInstanceVbo_  = 0;
  GLuint rockInstanceVbo_  = 0;
  GLuint fenceInstanceVbo_ = 0;

  // Single-instance VBO for outline rendering of one obstacle at a time.
  GLuint outlineInstanceVbo_ = 0;
  Kit    outlineTrunk_;
  Kit    outlineCanopy_;
  Kit    outlineRock_;
  Kit    outlineFence_;

  // Optional glTF tree model (replaces procedural trunk+canopy when loaded).
  Kit    treeTrunkGltf_;
  Kit    treeCanopyGltf_;
  GLuint treeGltfInstanceVbo_    = 0;
  Kit    outlineTreeTrunkGltf_;
  Kit    outlineTreeCanopyGltf_;
  GLuint outlineTreeGltfInstanceVbo_ = 0;
  bool      treeModelLoaded_    = false;
  glm::vec3 treeGltfAABBMin_   = glm::vec3(-0.45f, 0.0f, -0.45f);
  glm::vec3 treeGltfAABBMax_   = glm::vec3( 0.45f, 1.6f,  0.45f);

  std::size_t treeCount_  = 0;
  std::size_t rockCount_  = 0;
  std::size_t fenceCount_ = 0;

  // Object definitions cache (keyed by id string)
  std::unordered_map<std::string, ObjectDefCache> defs_;
};

}  // namespace world
