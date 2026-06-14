#pragma once

// 3D water "pool" tileset. For every water overlay tile (materialId == water),
// picks one of six carved pool meshes by 4-cardinal terrain/water adjacency (a
// rule-tile autotile) and draws it instanced + tilted to the tile's corner
// heights, so water tiles read as carved pools sitting flush in the terrain.
// The animated water surface (WaterMesh) renders separately, dropped slightly
// into the carve. Built-in tileset (fixed asset paths), shared by editor + game.

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "world/ModelLibrary.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace world {

class PoolRenderer {
public:
  // Resolver maps a relative model path → absolute. Registers the six pool
  // meshes on first call. Mirrors ObstacleSystem::setModelResolver.
  void setModelResolver(std::function<std::filesystem::path(const std::string&)> r);

  // Rebuild per-mesh instance lists from the map's water overlay tiles.
  void rebuildFromMap(const shared::WorldMapFile& map);

  void render(render::Shader& obstacleShader);   // static instanced draw
  void renderDepth(render::Shader& depthShader);  // shadow depth pass

  // Hot-reload: throttled (~2/sec) check of the pool .glb files' modification
  // times; if any changed on disk, reload all models and return true (so the
  // caller can rebuild instances). Used by the editor for live model edits.
  bool pollReloadIfChanged();

  bool empty() const { return instances_.empty(); }

private:
  void reloadModels();   // clear + re-ensure all six from disk

  ModelLibrary models_;
  bool         modelsInited_ = false;
  std::function<std::filesystem::path(const std::string&)> resolver_;
  std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_;
  std::chrono::steady_clock::time_point lastPoll_{};
  std::unordered_map<std::string, std::vector<ModelLibrary::Instance>> instances_;
};

}  // namespace world
