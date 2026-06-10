#pragma once

// Glaze JSON adapters for the types in SharedTypes.hpp.
//
// TileType needs explicit enumerate() meta (string-backed enum).
// NpcSpawn and WaterTile also have explicit object() meta to guarantee named-key
// JSON objects on all compilers. TileData and OnDisk use auto-reflection.
// TileData::obstacle is now a plain std::string — no glaze meta needed.

#include "shared/SharedTypes.hpp"

#include <filesystem>
#include <glaze/glaze.hpp>

template <>
struct glz::meta<shared::TileType> {
  using enum shared::TileType;
  static constexpr auto value = enumerate(
    "grass", grass,
    "dirt",  dirt,
    "stone", stone,
    "water", water,
    "cliff", cliff,
    "wall",  wall,
    "door",  door);
};

// Explicit metas for NpcSpawn and WaterTile ensure glaze always deserialises
// them as JSON objects with named keys, matching the manual-fprintf save format.
// Without this, glaze's auto-reflection on MSVC may silently produce a
// positional-array reader that can't parse {"kind":"chicken","tileX":5,...}.
template <>
struct glz::meta<shared::NpcSpawn> {
  using T = shared::NpcSpawn;
  static constexpr auto value = glz::object(
    "kind",  &T::kind,
    "tileX", &T::tileX,
    "tileY", &T::tileY);
};

template <>
struct glz::meta<shared::WaterTile> {
  using T = shared::WaterTile;
  static constexpr auto value = glz::object(
    "tileX", &T::tileX,
    "tileY", &T::tileY);
};

template <>
struct glz::meta<shared::OverlayTile> {
  using T = shared::OverlayTile;
  static constexpr auto value = glz::object(
    "tileX",      &T::tileX,
    "tileY",      &T::tileY,
    "shape",      &T::shape,
    "materialId", &T::materialId,
    "rotation",   &T::rotation);
};

template <>
struct glz::meta<shared::WallSeg> {
  using T = shared::WallSeg;
  static constexpr auto value = glz::object(
    "tileX",    &T::tileX,
    "tileY",    &T::tileY,
    "orient",   &T::orient,
    "pillar",   &T::pillar,
    "objectId", &T::objectId);
};

// ---- Map serialisation / deserialisation ---------------------------------
//
// The on-disk format is a JSON object with a "version" field (currently 2).
// loadWorldMap() reads that format back into a WorldMapFile + the editor-
// authored extensions (spawnPoint, npcSpawns).
//
// Note: the existing exportWorldMap() in App.cpp writes this format via
// manual fprintf; when we move it to a shared helper we'll use glaze instead.

namespace shared {

// Attempt to deserialise a saved worldMap.json into `out`.
// Returns true on success; writes an error to stderr on failure.
inline bool loadWorldMap(const std::filesystem::path& path, WorldMapFile& out) {
  // We read the whole file, then parse it with glaze.
  // The on-disk layout is a superset of WorldMapFile: it includes a "version"
  // int and a flat "vertexHeights" array alongside "tiles".  Glaze's
  // `glz::opts{.error_on_unknown_keys = false}` ignores keys we don't map.

  // Helper struct that mirrors the full on-disk shape so glaze can reflect it.
  struct OnDisk {
    int version   = 0;
    int width     = 64;
    int height    = 64;
    std::vector<std::vector<TileData>> tiles;
    std::vector<float>                 vertexHeights;
    std::array<int, 2>                 spawnPoint = {32, 32};
    std::vector<NpcSpawn>              npcSpawns;
    std::vector<WaterTile>             waterTiles;
    std::vector<OverlayTile>           overlayTiles;
    std::vector<WallSeg>               walls;
  };

  std::string buf;
  {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) {
      std::fprintf(stderr, "[loadWorldMap] cannot open %s\n", path.string().c_str());
      return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    buf.resize(static_cast<std::size_t>(sz));
    std::fread(buf.data(), 1, static_cast<std::size_t>(sz), f);
    std::fclose(f);
  }

  OnDisk disk;
  const auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(disk, buf);
  if (err) {
    std::fprintf(stderr, "[loadWorldMap] parse error: %s\n",
                 glz::format_error(err, buf).c_str());
    return false;
  }

  out.width        = disk.width;
  out.height       = disk.height;
  out.tiles        = std::move(disk.tiles);
  out.vertexHeights= std::move(disk.vertexHeights);
  out.spawnPoint   = disk.spawnPoint;
  out.npcSpawns    = std::move(disk.npcSpawns);
  out.waterTiles   = std::move(disk.waterTiles);
  out.overlayTiles = std::move(disk.overlayTiles);
  out.walls        = std::move(disk.walls);

  // Backward-compat: maps saved before the overlay system (v2 and earlier)
  // stored water as a flat waterTiles list. Migrate each into a full-tile
  // (shape 0) water overlay so the renderer has a single source of truth.
  // New maps (v3) carry overlayTiles directly and have an empty waterTiles.
  if (out.overlayTiles.empty() && !out.waterTiles.empty()) {
    out.overlayTiles.reserve(out.waterTiles.size());
    for (const auto& w : out.waterTiles)
      out.overlayTiles.push_back(OverlayTile{w.tileX, w.tileY, /*shape*/ 0,
                                             kWaterMaterialId});
  }

  // Rebuild x/y coordinates on every tile (they are stored in the JSON but
  // may be stale from older exports; recompute for correctness).
  for (int ty = 0; ty < out.height && ty < static_cast<int>(out.tiles.size()); ++ty)
    for (int tx = 0; tx < out.width && tx < static_cast<int>(out.tiles[ty].size()); ++tx) {
      out.tiles[ty][tx].x = tx;
      out.tiles[ty][tx].y = ty;
    }

  // Backward-compat: old maps stored obstacle as the string "none" (the old
  // enum serialisation).  Normalise to "" so every consumer can use .empty().
  for (int ty = 0; ty < out.height && ty < static_cast<int>(out.tiles.size()); ++ty)
    for (int tx = 0; tx < out.width && tx < static_cast<int>(out.tiles[ty].size()); ++tx)
      if (out.tiles[ty][tx].obstacle == "none")
        out.tiles[ty][tx].obstacle = "";

  // Walkability is authored per-tile and trusted as-is — water rendering is a
  // separate visual layer. This lets terrain raised above the waterline over a
  // water tile stay walkable while the water plane still renders. Painting the
  // water overlay material in the editor auto-blocks the tile (walkable=false).

  return true;
}

// Save a WorldMapFile to disk in the version-2 JSON format.
// Uses manual fprintf like the original exportWorldMap() for compatibility.
inline bool saveWorldMap(const std::filesystem::path& path,
                          const WorldMapFile& map) {
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) {
    std::fprintf(stderr, "[saveWorldMap] cannot open %s for writing\n",
                 path.string().c_str());
    return false;
  }

  auto hexOf = [](const std::string& s) -> std::string { return s; };

  std::fprintf(f, "{\n");
  std::fprintf(f, "  \"version\": 3,\n");
  std::fprintf(f, "  \"width\": %d,\n",  map.width);
  std::fprintf(f, "  \"height\": %d,\n", map.height);
  std::fprintf(f, "  \"spawnPoint\": [%d, %d],\n",
               map.spawnPoint[0], map.spawnPoint[1]);

  // npcSpawns
  std::fprintf(f, "  \"npcSpawns\": [");
  for (std::size_t i = 0; i < map.npcSpawns.size(); ++i) {
    const auto& n = map.npcSpawns[i];
    std::fprintf(f, "%s{\"kind\":\"%s\",\"tileX\":%d,\"tileY\":%d}",
                 i == 0 ? "" : ",", n.kind.c_str(), n.tileX, n.tileY);
  }
  std::fprintf(f, "],\n");

  // overlayTiles (OSRS-style shaped surface layer; supersedes waterTiles)
  std::fprintf(f, "  \"overlayTiles\": [");
  for (std::size_t i = 0; i < map.overlayTiles.size(); ++i) {
    const auto& o = map.overlayTiles[i];
    std::fprintf(f, "%s{\"tileX\":%d,\"tileY\":%d,\"shape\":%d,\"materialId\":%d,\"rotation\":%d}",
                 i == 0 ? "" : ",", o.tileX, o.tileY, o.shape, o.materialId, o.rotation);
  }
  std::fprintf(f, "],\n");

  // walls (wall + pillar edge features)
  std::fprintf(f, "  \"walls\": [");
  for (std::size_t i = 0; i < map.walls.size(); ++i) {
    const auto& w = map.walls[i];
    std::fprintf(f,
      "%s{\"tileX\":%d,\"tileY\":%d,\"orient\":%d,\"pillar\":%s,\"objectId\":\"%s\"}",
      i == 0 ? "" : ",", w.tileX, w.tileY, w.orient,
      w.pillar ? "true" : "false", w.objectId.c_str());
  }
  std::fprintf(f, "],\n");

  // tiles
  std::fprintf(f, "  \"tiles\": [\n");
  for (int ty = 0; ty < map.height; ++ty) {
    std::fprintf(f, "    [");
    for (int tx = 0; tx < map.width; ++tx) {
      const auto& t = map.tiles[ty][tx];

      auto typeStr = [&]() -> const char* {
        switch (t.type) {
          case TileType::grass:  return "grass";
          case TileType::dirt:   return "dirt";
          case TileType::stone:  return "stone";
          case TileType::water:  return "water";
          case TileType::cliff:  return "cliff";
          case TileType::wall:   return "wall";
          case TileType::door:   return "door";
        }
        return "grass";
      };
      // obstacle is now a plain std::string — write it directly.
      // Empty string means no obstacle; write "" to match the load path.
      std::fprintf(f,
        "%s{\"x\":%d,\"y\":%d,\"walkable\":%s,"
        "\"type\":\"%s\",\"obstacle\":\"%s\","
        "\"blocksRanged\":%s,\"groundColor\":\"%s\",\"height\":%.6f,"
        "\"obstacleRotation\":%d}",
        tx == 0 ? "" : ",",
        t.x, t.y,
        t.walkable     ? "true" : "false",
        typeStr(), t.obstacle.c_str(),
        t.blocksRanged ? "true" : "false",
        hexOf(t.groundColor).c_str(),
        t.height, t.obstacleRotation);
    }
    std::fprintf(f, "]%s\n", ty + 1 < map.height ? "," : "");
  }
  std::fprintf(f, "  ],\n");

  // vertexHeights
  std::fprintf(f, "  \"vertexHeights\": [");
  for (std::size_t i = 0; i < map.vertexHeights.size(); ++i) {
    if (i > 0) std::fprintf(f, ",");
    std::fprintf(f, "%.6f", map.vertexHeights[i]);
  }
  std::fprintf(f, "]\n}\n");

  std::fclose(f);
  return true;
}

}  // namespace shared
