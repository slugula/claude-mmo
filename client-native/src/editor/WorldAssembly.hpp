#pragma once

// Editor world assembly: merge all assigned chunk maps of a WorldManifest into
// one global WorldMapFile (global tile coords, unassigned cells void-filled),
// and slice a single chunk back out for saving. These mirror the SERVER's
// assembleWorld.ts + GameLoop.getChunkData so the editor and server agree on
// layout (including the vertex-row flip gr = gh-(cy+1)*S+vy).

#include "shared/SharedTypes.hpp"

#include <filesystem>
#include <set>
#include <utility>

namespace editor {

using CellKey = std::pair<int, int>;   // (cx, cy)

// Assemble every assigned chunk into `out` (global coords). `baseDir` is the
// manifest's directory; chunk mapFiles are resolved relative to it. `assigned`
// receives the set of cells that have map data. Returns false if any referenced
// chunk file is missing or not chunkSize×chunkSize (logs to stderr).
bool assembleWorld(const shared::WorldManifest& manifest,
                   const std::filesystem::path& baseDir,
                   shared::WorldMapFile& out,
                   std::set<CellKey>& assigned);

// Extract cell (cx,cy) from an assembled world as a local-coordinate (0..S)
// WorldMapFile ready to hand to shared::saveWorldMap. Inverse of the per-chunk
// copy in assembleWorld (same vertex-row flip).
shared::WorldMapFile sliceChunk(const shared::WorldMapFile& assembled,
                                int cx, int cy, int chunkSize,
                                const shared::WorldSpawnPoint& worldSpawn);

// Debug self-check: assemble, then for every assigned cell slice it back and
// compare the chunk's INTERIOR tiles + vertex heights against a fresh load of
// its file (interior avoids shared-edge last-writer noise). Logs PASS/FAIL.
// Run once during bring-up to trust the flip math before saving real edits.
void verifyAssembleRoundTrip(const shared::WorldManifest& manifest,
                             const std::filesystem::path& baseDir);

}  // namespace editor
