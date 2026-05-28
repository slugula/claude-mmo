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

  // Issue instanced draws for obstacles.
  // submergedPass=false (default): draws only obstacles on non-water tiles.
  // submergedPass=true: draws only obstacles on water tiles (post-water pass).
  // The shader must have all uniforms except u_color set by the caller.
  void render(render::Shader& obstacleShader, bool submergedPass = false);

  // Depth-only pass for shadow casting. The supplied shader is expected to
  // have its u_lightViewProj already bound; we just issue the instanced
  // draws against the same VAOs used for the regular pass. No u_color or
  // lighting uniforms are touched.
  void renderDepth(render::Shader& depthShader);

  // Render an outline around a single obstacle at the given tile. Uses
  // front-face culling + normal inflation for the "shell" outline effect.
  // The caller must have bound `outlineShader` and set u_viewProj already.
  // Returns false if the tile doesn't have a tree or rock.
  bool renderOutlineAt(render::Shader& outlineShader,
                       const shared::WorldMapFile& map,
                       int tileX, int tileY);

  // Render just the geometry for a single obstacle (no stencil / inflation).
  // Used by the screen-space outline mask pass to build a silhouette texture.
  // u_viewProj must already be set on the shader. Returns false if the tile
  // has no renderable obstacle.
  bool renderGeometryAt(render::Shader& maskShader,
                        const shared::WorldMapFile& map,
                        int tileX, int tileY);

  std::size_t treeCount()  const { return treeCount_;  }
  std::size_t rockCount()  const { return rockCount_;  }
  std::size_t fenceCount() const { return fenceCount_; }
  std::size_t treeSubCount()  const { return treeSubCount_;  }
  std::size_t rockSubCount()  const { return rockSubCount_;  }
  std::size_t fenceSubCount() const { return fenceSubCount_; }

  // Axis-aligned bounding box of the gltf tree model in world space (after
  // applying kScaleXZ/kScaleY). Valid only when treeModelLoaded() is true.
  bool        treeModelLoaded()   const { return treeModelLoaded_; }
  glm::vec3   treeGltfAABBMin()   const { return treeGltfAABBMin_; }
  glm::vec3   treeGltfAABBMax()   const { return treeGltfAABBMax_; }

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
  Kit fence_;

  GLuint treeInstanceVbo_  = 0;  // above-water instances (shared trunk+canopy)
  GLuint rockInstanceVbo_  = 0;
  GLuint fenceInstanceVbo_ = 0;

  // Submerged (below-water) variants — separate VBOs so no BaseInstance needed.
  Kit    trunkSub_;
  Kit    canopySub_;
  Kit    rockSub_;
  Kit    fenceSub_;
  GLuint treeInstanceVboSub_  = 0;
  GLuint rockInstanceVboSub_  = 0;
  GLuint fenceInstanceVboSub_ = 0;
  // glTF submerged variant
  Kit    treeTrunkGltfSub_;
  Kit    treeCanopyGltfSub_;
  GLuint treeGltfInstanceVboSub_ = 0;
  // Single-instance VBO for outline rendering of one obstacle at a time.
  GLuint outlineInstanceVbo_ = 0;
  Kit    outlineTrunk_;   // VAOs bound to outlineInstanceVbo_
  Kit    outlineCanopy_;
  Kit    outlineRock_;
  Kit    outlineFence_;

  // Optional glTF tree model (replaces procedural trunk+canopy when loaded).
  // Each instance sits at the centre of a 2×2 tile block.
  Kit    treeTrunkGltf_;
  Kit    treeCanopyGltf_;
  GLuint treeGltfInstanceVbo_    = 0;
  Kit    outlineTreeTrunkGltf_;
  Kit    outlineTreeCanopyGltf_;
  GLuint outlineTreeGltfInstanceVbo_ = 0;
  bool      treeModelLoaded_    = false;
  glm::vec3 treeGltfAABBMin_   = glm::vec3(-0.45f, 0.0f, -0.45f);  // fallback = procedural bounds
  glm::vec3 treeGltfAABBMax_   = glm::vec3( 0.45f, 1.6f,  0.45f);

  std::size_t treeCount_    = 0;  // above-water count
  std::size_t rockCount_    = 0;
  std::size_t fenceCount_   = 0;
  std::size_t treeSubCount_ = 0;  // submerged count
  std::size_t rockSubCount_ = 0;
  std::size_t fenceSubCount_= 0;

  // Object definitions cache (keyed by id string)
  std::unordered_map<std::string, ObjectDefCache> defs_;
};

}  // namespace world
