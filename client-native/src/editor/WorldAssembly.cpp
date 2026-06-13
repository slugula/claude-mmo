#include "editor/WorldAssembly.hpp"
#include "shared/SharedTypesJson.hpp"   // loadWorldMap

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace editor {

namespace {

shared::TileData voidTile(int x, int y) {
  shared::TileData t;
  t.x = x; t.y = y;
  t.walkable = false;
  t.isVoid = true;
  t.type = shared::TileType::grass;
  t.obstacle = "";
  t.blocksRanged = false;
  t.groundColor = "#1c2026";   // dark neutral; matches server voidTile
  t.height = 0.0f;
  return t;
}

}  // namespace

bool assembleWorld(const shared::WorldManifest& manifest,
                   const std::filesystem::path& baseDir,
                   shared::WorldMapFile& out,
                   std::set<CellKey>& assigned) {
  const int S = manifest.chunkSize;
  if (S <= 0 || manifest.chunks.empty()) {
    std::fprintf(stderr, "[assembleWorld] empty manifest or bad chunkSize\n");
    return false;
  }

  int maxCx = 0, maxCy = 0;
  for (const auto& c : manifest.chunks) {
    maxCx = std::max(maxCx, c.cx);
    maxCy = std::max(maxCy, c.cy);
  }
  const int gw = (maxCx + 1) * S;
  const int gh = (maxCy + 1) * S;

  out = {};
  out.width = gw;
  out.height = gh;
  out.spawnPoint = { manifest.spawn.x, manifest.spawn.y };
  out.tiles.assign(static_cast<std::size_t>(gh),
                   std::vector<shared::TileData>(static_cast<std::size_t>(gw)));
  for (int y = 0; y < gh; ++y)
    for (int x = 0; x < gw; ++x) out.tiles[y][x] = voidTile(x, y);
  out.vertexHeights.assign(static_cast<std::size_t>((gw + 1)) * (gh + 1), 0.0f);
  assigned.clear();

  int mismatches = 0;
  std::unordered_map<long long, bool> vertexWritten;

  for (const auto& chunk : manifest.chunks) {
    shared::WorldMapFile data;
    if (!shared::loadWorldMap(baseDir / chunk.mapFile, data)) {
      std::fprintf(stderr, "[assembleWorld] cannot load chunk %s\n", chunk.mapFile.c_str());
      return false;
    }
    if (data.width != S || data.height != S) {
      std::fprintf(stderr, "[assembleWorld] %s is %dx%d; chunkSize is %d\n",
                   chunk.mapFile.c_str(), data.width, data.height, S);
      return false;
    }
    const int ox = chunk.cx * S, oy = chunk.cy * S;
    assigned.insert({ chunk.cx, chunk.cy });

    for (int y = 0; y < S; ++y) {
      for (int x = 0; x < S; ++x) {
        const int gx = ox + x, gy = oy + y;
        shared::TileData t = data.tiles[y][x];
        t.x = gx; t.y = gy; t.isVoid = false;
        out.tiles[gy][gx] = std::move(t);
      }
    }

    // Vertex heights into the flipped band: local row vy → global row
    // grBase+vy, grBase = gh-(cy+1)*S. (Mirror of assembleWorld.ts.)
    if (static_cast<int>(data.vertexHeights.size()) == (S + 1) * (S + 1)) {
      const int grBase = gh - (chunk.cy + 1) * S;
      for (int vy = 0; vy <= S; ++vy) {
        for (int vx = 0; vx <= S; ++vx) {
          const std::size_t gi =
              static_cast<std::size_t>(grBase + vy) * (gw + 1) + (ox + vx);
          const float v = data.vertexHeights[static_cast<std::size_t>(vy) * (S + 1) + vx];
          const long long key = static_cast<long long>(gi);
          if (vertexWritten.count(key) && std::abs(out.vertexHeights[gi] - v) > 1e-4f)
            ++mismatches;
          out.vertexHeights[gi] = v;
          vertexWritten[key] = true;
        }
      }
    }

    for (auto n : data.npcSpawns) { n.tileX += ox; n.tileY += oy; out.npcSpawns.push_back(std::move(n)); }
    for (auto w : data.walls)     { w.tileX += ox; w.tileY += oy; out.walls.push_back(std::move(w)); }
    for (auto o : data.overlayTiles) { o.tileX += ox; o.tileY += oy; out.overlayTiles.push_back(std::move(o)); }
  }

  std::fprintf(stdout, "[assembleWorld] %zu chunk(s) -> %dx%d world (%zu npcs, %zu walls, %zu overlays%s)\n",
               manifest.chunks.size(), gw, gh,
               out.npcSpawns.size(), out.walls.size(), out.overlayTiles.size(),
               mismatches ? "; shared-edge mismatches present" : "");
  return true;
}

shared::WorldMapFile sliceChunk(const shared::WorldMapFile& a,
                                int cx, int cy, int S,
                                const shared::WorldSpawnPoint& worldSpawn) {
  const int gw = a.width, gh = a.height;
  const int ox = cx * S, oy = cy * S;

  shared::WorldMapFile out;
  out.width = S; out.height = S;
  out.tiles.assign(static_cast<std::size_t>(S),
                   std::vector<shared::TileData>(static_cast<std::size_t>(S)));
  for (int y = 0; y < S; ++y) {
    for (int x = 0; x < S; ++x) {
      shared::TileData t = a.tiles[oy + y][ox + x];
      t.x = x; t.y = y; t.isVoid = false;
      out.tiles[y][x] = std::move(t);
    }
  }

  // Vertex heights: inverse of the assembly flip — local (vy,vx) reads global
  // row grBase+vy, col ox+vx (grBase = gh-(cy+1)*S).
  out.vertexHeights.assign(static_cast<std::size_t>((S + 1)) * (S + 1), 0.0f);
  const int grBase = gh - (cy + 1) * S;
  for (int vy = 0; vy <= S; ++vy)
    for (int vx = 0; vx <= S; ++vx) {
      const std::size_t gi = static_cast<std::size_t>(grBase + vy) * (gw + 1) + (ox + vx);
      out.vertexHeights[static_cast<std::size_t>(vy) * (S + 1) + vx] =
          (gi < a.vertexHeights.size()) ? a.vertexHeights[gi] : 0.0f;
    }

  auto inCell = [&](int tx, int ty) {
    return tx >= ox && tx < ox + S && ty >= oy && ty < oy + S;
  };
  for (auto n : a.npcSpawns)    if (inCell(n.tileX, n.tileY)) { n.tileX -= ox; n.tileY -= oy; out.npcSpawns.push_back(std::move(n)); }
  for (auto w : a.walls)        if (inCell(w.tileX, w.tileY)) { w.tileX -= ox; w.tileY -= oy; out.walls.push_back(std::move(w)); }
  for (auto o : a.overlayTiles) if (inCell(o.tileX, o.tileY)) { o.tileX -= ox; o.tileY -= oy; out.overlayTiles.push_back(std::move(o)); }

  // Per-chunk spawnPoint is vestigial in world mode (the world spawn lives in
  // the manifest); set it to the world spawn if it lands in this cell, else
  // the chunk centre. Non-destructive enough for the legacy field.
  if (inCell(worldSpawn.x, worldSpawn.y))
    out.spawnPoint = { worldSpawn.x - ox, worldSpawn.y - oy };
  else
    out.spawnPoint = { S / 2, S / 2 };

  return out;
}

void verifyAssembleRoundTrip(const shared::WorldManifest& manifest,
                             const std::filesystem::path& baseDir) {
  const int S = manifest.chunkSize;
  shared::WorldMapFile assembled;
  std::set<CellKey> assigned;
  if (!assembleWorld(manifest, baseDir, assembled, assigned)) {
    std::fprintf(stderr, "[roundtrip] assemble failed\n");
    return;
  }
  int cellsOk = 0, cellsBad = 0;
  for (const auto& chunk : manifest.chunks) {
    shared::WorldMapFile src;
    if (!shared::loadWorldMap(baseDir / chunk.mapFile, src)) continue;
    shared::WorldMapFile sl = sliceChunk(assembled, chunk.cx, chunk.cy, S, manifest.spawn);

    bool ok = true;
    // Interior tiles (skip edges) + interior vertices avoid shared-edge noise.
    for (int y = 1; y < S - 1 && ok; ++y)
      for (int x = 1; x < S - 1; ++x)
        if (src.tiles[y][x].groundColor != sl.tiles[y][x].groundColor ||
            src.tiles[y][x].obstacle    != sl.tiles[y][x].obstacle) { ok = false; break; }
    if (static_cast<int>(src.vertexHeights.size()) == (S + 1) * (S + 1))
      for (int vy = 1; vy < S && ok; ++vy)
        for (int vx = 1; vx < S; ++vx)
          if (std::abs(src.vertexHeights[vy * (S + 1) + vx] -
                       sl.vertexHeights[vy * (S + 1) + vx]) > 1e-4f) { ok = false; break; }
    if (ok) ++cellsOk; else { ++cellsBad;
      std::fprintf(stderr, "[roundtrip] cell (%d,%d) %s MISMATCH\n", chunk.cx, chunk.cy, chunk.mapFile.c_str()); }
  }
  std::fprintf(stdout, "[roundtrip] %d/%d cells OK%s\n",
               cellsOk, cellsOk + cellsBad, cellsBad ? " — FAIL" : " — PASS");
}

}  // namespace editor
