#pragma once

// Hand-written subset of src/shared/types.ts + src/shared/constants.ts.
// Map I/O is procedural in-process; networking (Phase 4) adds PlayerState,
// NPCState, DroppedItemState, InitMessage, StateMessage, and a small set of
// GameAction variants.
//
// TODO(post-phase-11): replace these declarations with the output of a
// TypeScript -> C++ codegen script.

#include <array>
#include <cstdint>
#include <optional>
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

// ---- TileData ------------------------------------------------------------

struct TileData {
  int          x            = 0;
  int          y            = 0;
  bool         walkable     = true;
  TileType     type         = TileType::grass;
  std::string  obstacle     = "";   // empty = none; otherwise a DB object ID e.g. "tree"
  bool         blocksRanged = false;
  std::string  groundColor  = "#7ec850";
  float        height       = 0.0f;
};

// ---- NPC spawn descriptor (used by the level editor + server) ------------

struct NpcSpawn {
  std::string kind;      // "chicken", "shopkeeper"
  int         tileX = 0;
  int         tileY = 0;
};

// ---- Water tile descriptor (used by the level editor + client renderer) --

struct WaterTile {
  int tileX = 0;
  int tileY = 0;
};

// ---- In-memory map (produced by MapGenerator or loaded from JSON) --------

struct WorldMapFile {
  int                                width  = 0;
  int                                height = 0;
  std::vector<std::vector<TileData>> tiles;
  std::vector<float>                 vertexHeights;
  // Editor-authored fields (present in saved maps; default-constructed when
  // the map is generated procedurally by the client).
  std::array<int, 2>                 spawnPoint  = {32, 32};
  std::vector<NpcSpawn>              npcSpawns;
  std::vector<WaterTile>             waterTiles;
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

// One inventory / equipment slot. Item icons (PNG) aren't loaded yet;
// panels display `itemId` as a short text label.
struct ItemStack {
  std::string itemId;
  int         quantity = 0;
};

// SkillsState in TS is Record<SkillId, SkillState>. We deserialize as a
// flat map so any future skill the server adds shows up automatically.
struct SkillState {
  // Server XP is fractional (e.g. mining 17.5/ore, combat hitpoints = dmg×1.33),
  // so this must be floating point — an int here fails JSON parsing on any
  // non-integer total ("parse_number_failure").
  double xp = 0;
  int level = 1;
};

// Mirror of PlayerState — partial: only the fields the native client needs
// for rendering and pathing. The server's response carries more (inventory,
// skills, equipment, chat, etc.); we skip those for Phase 4.
struct PlayerState {
  int                                          tileX          = 0;
  int                                          tileY          = 0;
  std::string                                  facing;          // cardinal + intercardinal: "north" / "north_east" / "east" / ... / "north_west"
  std::vector<GridPosition>                    path;
  int                                          destinationX   = 0;
  int                                          destinationY   = 0;
  int                                          hp             = 0;
  int                                          maxHp          = 0;
  std::string                                  playerName;
  bool                                         dying          = false;
  // Phase 8 — fields needed by the UI panels.
  std::vector<std::optional<ItemStack>>        inventory;
  std::vector<std::optional<ItemStack>>        bank;
  std::unordered_map<std::string, ItemStack>   equipped;
  std::unordered_map<std::string, SkillState>  skills;
  std::string                                  chatMessage;
  int                                          chatMessageTick = -999;
  int                                          lastHitTick     = -999;
  int                                          lastHitDamage   = 0;
  // Tick stamps used by the client to detect per-tick action events; the
  // value rises monotonically when the server validates an attack / chop.
  int                                          lastAttackTick  = -999;
  int                                          lastChopTick    = -999;
  // Set while the server has a pending pick-up queued for this player.
  // Transitions from has_value() → nullopt indicate a completed pickup.
  std::optional<std::string>                   pickupItemId;
};

// Partial NPCState — enough to render at the right place + face.
struct NPCState {
  std::string id;
  std::string kind;
  int         tileX         = 0;
  int         tileY         = 0;
  std::string facing;
  int         hp            = 0;
  int         maxHp         = 0;
  bool        dying         = false;
  int         lastHitTick   = -999;
  int         lastHitDamage = 0;
};

struct DroppedItemState {
  std::string id;
  std::string itemId;
  int         tileX = 0;
  int         tileY = 0;
};

// Minimal envelope used to peek the "type" field before re-parsing the body
// as the matching message struct. Glaze can't auto-reflect function-local
// types (no external linkage), so this lives at namespace scope.
struct MessageHeader {
  std::string type;
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
  // Per-player system messages (keyed by playerId). Contains feedback like
  // "Shopkeeper: Welcome..." or "I can't reach that." etc.
  std::unordered_map<std::string, std::vector<std::string>> messages;
  // Resource nodes the server reports as depleted (key="x-y" → respawnAtTick).
  // The client swaps these tiles to the object's depleted-model variant.
  std::unordered_map<std::string, int> depletedTrees;
  std::unordered_map<std::string, int> depletedRocks;
};

// Client -> server: actions queued by the player.
// Phase 4 ships only MOVE_TO; further variants come in later phases.
struct MoveToAction {
  int targetX = 0;
  int targetY = 0;
};

}  // namespace shared
