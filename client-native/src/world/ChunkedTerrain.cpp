#include "world/ChunkedTerrain.hpp"

#include "shared/SharedTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace world {

namespace {

// Extract the 6 frustum planes (xyz = normal, w = d) from a viewProj matrix
// (Gribb–Hartmann). Normals point inward; a point p is inside a plane when
// dot(n, p) + d >= 0.
std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& m) {
  std::array<glm::vec4, 6> p;
  for (int i = 0; i < 3; ++i) {
    p[i * 2 + 0] = glm::vec4(m[0][3] + m[0][i], m[1][3] + m[1][i],
                             m[2][3] + m[2][i], m[3][3] + m[3][i]);
    p[i * 2 + 1] = glm::vec4(m[0][3] - m[0][i], m[1][3] - m[1][i],
                             m[2][3] - m[2][i], m[3][3] - m[3][i]);
  }
  return p;
}

bool aabbInFrustum(const std::array<glm::vec4, 6>& planes,
                   const glm::vec3& mn, const glm::vec3& mx) {
  for (const auto& pl : planes) {
    // p-vertex: the AABB corner furthest along the plane normal.
    const glm::vec3 v(pl.x >= 0.0f ? mx.x : mn.x,
                      pl.y >= 0.0f ? mx.y : mn.y,
                      pl.z >= 0.0f ? mx.z : mn.z);
    if (glm::dot(glm::vec3(pl), v) + pl.w < 0.0f) return false;
  }
  return true;
}

}  // namespace

void ChunkedTerrain::computeAabb(Chunk& c) const {
  const int W = map_->width;
  const int H = map_->height;
  float minH = 0.0f, maxH = 0.0f;
  bool first = true;
  // Vertex rows covering tile rows [y0, y0+h): mesh rows H-y0-h .. H-y0.
  for (int gr = H - c.y0 - c.h; gr <= H - c.y0; ++gr) {
    for (int gc = c.x0; gc <= c.x0 + c.w; ++gc) {
      const std::size_t idx = static_cast<std::size_t>(gr) * (W + 1) + gc;
      if (idx >= map_->vertexHeights.size()) continue;
      const float v = map_->vertexHeights[idx] * shared::kMaxTerrainH;
      if (first) { minH = maxH = v; first = false; }
      else { minH = std::min(minH, v); maxH = std::max(maxH, v); }
    }
  }
  // World X spans [x0-0.5, x0+w-0.5]; tile y spans world Z likewise.
  c.aabbMin = { static_cast<float>(c.x0) - 0.5f,        minH - 0.1f,
                static_cast<float>(c.y0) - 0.5f };
  c.aabbMax = { static_cast<float>(c.x0 + c.w) - 0.5f,  maxH + 0.1f,
                static_cast<float>(c.y0 + c.h) - 0.5f };
}

void ChunkedTerrain::buildChunk(Chunk& c) {
  // All-void chunks (unassigned world cells) produce no triangles — mark empty
  // so we never retry every frame.
  auto data = buildTerrainMeshRect(*map_, c.x0, c.y0, c.w, c.h);
  if (data.triangleIndices.empty()) {
    c.empty = true;
    c.built = true;
    return;
  }
  c.mesh.upload(data.positions, data.colors, data.triangleIndices,
                data.lineIndices, data.normals);
  c.built = true;
}

void ChunkedTerrain::reset(const shared::WorldMapFile* map,
                           int centerTileX, int centerTileY, int ring) {
  chunks_.clear();
  map_ = map;
  chunksX_ = chunksY_ = 0;
  if (!map_ || map_->width <= 0 || map_->height <= 0) return;

  chunksX_ = (map_->width  + kChunkTiles - 1) / kChunkTiles;
  chunksY_ = (map_->height + kChunkTiles - 1) / kChunkTiles;
  chunks_.resize(static_cast<std::size_t>(chunksX_) * chunksY_);
  for (int cy = 0; cy < chunksY_; ++cy) {
    for (int cx = 0; cx < chunksX_; ++cx) {
      Chunk& c = chunks_[static_cast<std::size_t>(cy) * chunksX_ + cx];
      c.x0 = cx * kChunkTiles;
      c.y0 = cy * kChunkTiles;
      c.w  = std::min(kChunkTiles, map_->width  - c.x0);
      c.h  = std::min(kChunkTiles, map_->height - c.y0);
      computeAabb(c);
    }
  }
  // Synchronous first fill so the world isn't full of holes on the first frame.
  update(centerTileX, centerTileY, ring, chunksX_ * chunksY_);
}

void ChunkedTerrain::update(int centerTileX, int centerTileY, int ring, int budget) {
  if (!map_ || chunks_.empty()) return;
  const int ccx = std::clamp(centerTileX / kChunkTiles, 0, chunksX_ - 1);
  const int ccy = std::clamp(centerTileY / kChunkTiles, 0, chunksY_ - 1);

  // Build missing meshes nearest-first (ring distance 0, 1, 2, ...).
  for (int d = 0; d <= ring && budget > 0; ++d) {
    for (int cy = ccy - d; cy <= ccy + d && budget > 0; ++cy) {
      if (cy < 0 || cy >= chunksY_) continue;
      for (int cx = ccx - d; cx <= ccx + d && budget > 0; ++cx) {
        if (cx < 0 || cx >= chunksX_) continue;
        if (std::max(std::abs(cx - ccx), std::abs(cy - ccy)) != d) continue;
        Chunk& c = chunks_[static_cast<std::size_t>(cy) * chunksX_ + cx];
        if (!c.built) { buildChunk(c); --budget; }
      }
    }
  }

  // Evict meshes outside ring+1 so memory tracks the ring.
  for (int cy = 0; cy < chunksY_; ++cy) {
    for (int cx = 0; cx < chunksX_; ++cx) {
      Chunk& c = chunks_[static_cast<std::size_t>(cy) * chunksX_ + cx];
      if (!c.built || c.empty) continue;
      if (std::max(std::abs(cx - ccx), std::abs(cy - ccy)) > ring + 1) {
        c.mesh = render::Mesh{};   // release GL buffers
        c.built = false;
      }
    }
  }
}

void ChunkedTerrain::markTileDirty(int gx, int gy) {
  if (!map_ || chunks_.empty()) return;
  const int cx = gx / kChunkTiles, cy = gy / kChunkTiles;
  if (cx < 0 || cy < 0 || cx >= chunksX_ || cy >= chunksY_) return;
  Chunk& c = chunks_[static_cast<std::size_t>(cy) * chunksX_ + cx];
  // Recompute the AABB (heights just changed) and force a rebuild; clear the
  // empty flag so a chunk that was all-void before now gets geometry.
  computeAabb(c);
  c.mesh  = render::Mesh{};
  c.built = false;
  c.empty = false;
}

void ChunkedTerrain::draw(const glm::mat4& viewProj) const {
  const auto planes = frustumPlanes(viewProj);
  for (const auto& c : chunks_) {
    if (!c.built || c.empty) continue;
    if (!aabbInFrustum(planes, c.aabbMin, c.aabbMax)) continue;
    c.mesh.draw();
  }
}

void ChunkedTerrain::drawLines() const {
  for (const auto& c : chunks_)
    if (c.built && !c.empty) c.mesh.drawLines();
}

std::vector<ChunkedTerrain::ChunkRect> ChunkedTerrain::residentRects() const {
  std::vector<ChunkRect> out;
  out.reserve(chunks_.size());
  for (const auto& c : chunks_) {
    if (!c.built || c.empty) continue;
    out.push_back({ c.x0, c.y0, c.w, c.h, c.aabbMin, c.aabbMax });
  }
  return out;
}

}  // namespace world
