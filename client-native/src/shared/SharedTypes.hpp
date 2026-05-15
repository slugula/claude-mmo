#pragma once

// Hand-written subset of src/shared/types.ts + src/shared/constants.ts
// covering exactly what the terrain pipeline needs (Phase 1).
//
// Map I/O is deferred until after Phase 11 — for now the world is generated
// procedurally in-process, so the legacy worldMap.json shape is not modeled
// here. NPC spawns, permanent items, and other map-file fields will return
// when we revisit the editor flow.
//
// TODO(post-phase-11): replace these declarations with the output of a
// TypeScript -> C++ codegen script driven by ts-morph, parsing src/shared/types.ts.

#include <string>
#include <vector>

namespace shared {

// ---- Constants (mirror src/shared/constants.ts) ---------------------------

constexpr float kTileSize    = 1.0f;
constexpr float kMaxTerrainH = 4.0f;   // matches MAX_TERRAIN_H in src/world/World.ts
constexpr float kWaterY      = -0.25f; // matches WATER_Y in src/world/World.ts

// ---- Tile enums (string union in TS, enum class here) ---------------------

enum class TileType {
  grass,
  dirt,
  stone,
  water,
  cliff,
  wall,
  door,
};

enum class ObstacleType {
  tree,
  rock,
  chest,
  fishing_spot,
  none,
};

// ---- TileData (mirrors interface in types.ts) -----------------------------

struct TileData {
  int          x            = 0;
  int          y            = 0;
  bool         walkable     = true;
  TileType     type         = TileType::grass;
  ObstacleType obstacle     = ObstacleType::none;
  bool         blocksRanged = false;
  std::string  groundColor  = "#7ec850";
  float        height       = 0.0f;
};

// ---- WorldMapFile -- in-memory world description ---------------------------
//
// Currently produced by MapGenerator. After Phase 11 this will also be filled
// by a loader for whatever map format the refactored editor saves.
struct WorldMapFile {
  int                width  = 0;
  int                height = 0;
  std::vector<std::vector<TileData>> tiles;
  // Per-vertex heights — flat row-major array, length (width+1)*(height+1).
  // Matches the Babylon vertex layout (row 0 = far z, row H = near z).
  std::vector<float> vertexHeights;
};

}  // namespace shared
