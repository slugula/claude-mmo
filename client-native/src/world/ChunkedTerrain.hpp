#pragma once

// Chunked terrain rendering for large (multi-chunk overworld) maps.
//
// Slices the flat assembled map into fixed 64×64-tile render chunks — a pure
// rendering granularity, independent of the world manifest's authoring chunks.
// Each chunk builds its mesh via buildTerrainMeshRect (world-space vertices, so
// no per-chunk transform; border shading samples the full map and is seam-free).
//
// Per frame, update() keeps meshes resident for chunks within a Chebyshev ring
// around the camera/player (building at most `budget` per call to avoid
// hitches; the first fill after reset() is synchronous), and draw() renders the
// resident set with AABB-vs-frustum culling.

#include "render/Mesh.hpp"
#include "world/TerrainBuilder.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace world {

class ChunkedTerrain {
public:
  static constexpr int kChunkTiles = 64;   // render-chunk side length in tiles

  // Re-slice for a new/changed map. The map must outlive this object (the
  // pointer is read during lazy builds). Pass nullptr to release everything.
  // Builds the ring around (centerX, centerY) synchronously so the first
  // rendered frame is complete.
  void reset(const shared::WorldMapFile* map, int centerTileX, int centerTileY, int ring);

  // Keep the ring around the center resident: lazily build up to `budget`
  // missing chunk meshes (nearest first) and evict meshes outside ring+1.
  void update(int centerTileX, int centerTileY, int ring, int budget = 2);

  // Mark the render chunk covering global tile (gx,gy) as needing a (re)build —
  // used when streamed tile data arrives for a previously-void/unbuilt chunk.
  // The next update() rebuilds it if it's within the ring.
  void markTileDirty(int gx, int gy);

  // Draw resident chunk meshes whose AABB intersects the view frustum.
  // The terrain shader must already be bound with its uniforms set.
  void draw(const glm::mat4& viewProj) const;
  void drawLines() const;            // wireframe overlay (no culling; debug)

  bool valid() const { return map_ != nullptr; }
  int  chunksX() const { return chunksX_; }
  int  chunksY() const { return chunksY_; }

  // Visible-chunk tile rects for the picker: chunks currently resident, with
  // their world-space AABBs for near-to-far ray traversal.
  struct ChunkRect {
    int x0 = 0, y0 = 0, w = 0, h = 0;   // tile rect
    glm::vec3 aabbMin{0.0f}, aabbMax{0.0f};
  };
  std::vector<ChunkRect> residentRects() const;

private:
  struct Chunk {
    render::Mesh mesh;
    bool      built  = false;
    bool      empty  = false;          // all-void chunk → no mesh at all
    glm::vec3 aabbMin{0.0f}, aabbMax{0.0f};
    int       x0 = 0, y0 = 0, w = 0, h = 0;
  };

  void buildChunk(Chunk& c);
  void computeAabb(Chunk& c) const;

  const shared::WorldMapFile* map_ = nullptr;
  std::vector<Chunk> chunks_;        // row-major [cy * chunksX_ + cx]
  int chunksX_ = 0;
  int chunksY_ = 0;
};

}  // namespace world
