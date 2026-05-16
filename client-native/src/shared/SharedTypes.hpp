#pragma once

// Hand-written subset of src/shared/types.ts + src/shared/constants.ts.
// Map I/O is procedural in-process; networking (Phase 4) adds PlayerState,
// NPCState, DroppedItemState, InitMessage, StateMessage, and a small set of
// GameAction variants.
//
// TODO(post-phase-11): replace these declarations with the output of a
// TypeScript -> C++ codegen script.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace shared {

// ---- Constants (mirror src/shared/constants.ts) ---------------------------

constexpr float kTileSize         = 1.0f;
constexpr float kMaxTerrainH      = 4.0f;
constexpr float kWaterY           = -0.25f;
constexpr int   kTickDurationMs   = 200;

// ---- Tile enums ----------------------------------------------------------

enum class TileType {
  grass, dirt, stone, water, cliff, wall, door,
};

enum class ObstacleType {
  tree, rock, chest, fishing_spot, none,
};

// ---- TileData ------------------------------------------------------------

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

// ---- In-memory map (produced by MapGenerator) ----------------------------

struct WorldMapFile {
  int                                width  = 0;
  int                                height = 0;
  std::vector<std::vector<TileData>> tiles;
  std::vector<float>                 vertexHeights;
};

// =====================================================================
// Phase 4 — network messages and the entities they carry
// =====================================================================
//
// We deserialize a minimal subset of each: glaze is told to ignore unknown
// keys so the server can carry richer state without breaking the client.

struct GridPosition {
  int x = 0;
  int y = 0;
};

// Mirror of PlayerState — partial: only the fields the native client needs
// for rendering and pathing. The server's response carries more (inventory,
// skills, equipment, chat, etc.); we skip those for Phase 4.
struct PlayerState {
  int                       tileX          = 0;
  int                       tileY          = 0;
  std::string               facing;          // "north" / "south" / "east" / "west"
  std::vector<GridPosition> path;
  int                       destinationX   = 0;
  int                       destinationY   = 0;
  int                       hp             = 0;
  int                       maxHp          = 0;
  std::string               playerName;
};

// Partial NPCState — enough to render a placeholder, no behaviour yet.
struct NPCState {
  std::string id;
  std::string kind;
  int         tileX       = 0;
  int         tileY       = 0;
  std::string facing;
  int         hp          = 0;
};

struct DroppedItemState {
  std::string id;
  std::string itemId;
  int         tileX = 0;
  int         tileY = 0;
};

// Server -> client: init (sent once on connection)
struct InitMessage {
  std::string                        type;            // "init"
  std::string                        playerId;
  std::vector<std::vector<TileData>> tiles;           // present but unused — we keep our procedural map
  std::vector<float>                 vertexHeights;   // ditto
  bool                               isNewPlayer = false;
};

// Server -> client: state patch (sent every 200 ms tick)
struct StateMessage {
  std::string                                  type;          // "state"
  int                                          tick = 0;
  std::unordered_map<std::string, PlayerState> players;
  std::vector<NPCState>                        npcs;
  std::vector<DroppedItemState>                droppedItems;
};

// Client -> server: actions queued by the player.
// Phase 4 ships only MOVE_TO; further variants come in later phases.
struct MoveToAction {
  int targetX = 0;
  int targetY = 0;
};

}  // namespace shared
