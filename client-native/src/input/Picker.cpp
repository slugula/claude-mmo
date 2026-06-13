#include "input/Picker.hpp"
#include "shared/SharedTypes.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <limits>

#include <algorithm>

namespace input {

namespace {

// Ray-vs-AABB slab test; writes the entry distance (clamped to 0 when the
// origin is inside) on hit.
bool rayAabb(const glm::vec3& orig, const glm::vec3& dir,
             const glm::vec3& mn, const glm::vec3& mx, float* outTEnter) {
  float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
  for (int i = 0; i < 3; ++i) {
    if (std::abs(dir[i]) < 1e-9f) {
      if (orig[i] < mn[i] || orig[i] > mx[i]) return false;
      continue;
    }
    const float inv = 1.0f / dir[i];
    float t0 = (mn[i] - orig[i]) * inv;
    float t1 = (mx[i] - orig[i]) * inv;
    if (t0 > t1) std::swap(t0, t1);
    tMin = std::max(tMin, t0);
    tMax = std::min(tMax, t1);
    if (tMin > tMax) return false;
  }
  *outTEnter = tMin;
  return true;
}

// Heightfield test over a tile rect; updates best/bestT in place.
void pickTilesInRect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                     const std::vector<float>& vh, int W, int H,
                     int rx0, int ry0, int rw, int rh,
                     PickResult& best, float& bestT) {
  const int x1 = std::min(W, rx0 + rw);
  const int y1 = std::min(H, ry0 + rh);
  for (int ty = std::max(0, ry0); ty < y1; ++ty) {
    for (int tx = std::max(0, rx0); tx < x1; ++tx) {
      const float hSW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
      const float hSE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
      const float hNW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
      const float hNE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;

      const glm::vec3 SW(tx - 0.5f, hSW, ty - 0.5f);
      const glm::vec3 SE(tx + 0.5f, hSE, ty - 0.5f);
      const glm::vec3 NW(tx - 0.5f, hNW, ty + 0.5f);
      const glm::vec3 NE(tx + 0.5f, hNE, ty + 0.5f);

      float t;
      if (rayTriangle(rayOrigin, rayDir, NW, SW, NE, &t) && t < bestT) {
        bestT = t;
        best.hit = true; best.tileX = tx; best.tileY = ty;
        best.worldPos = rayOrigin + rayDir * t; best.rayT = t;
      }
      if (rayTriangle(rayOrigin, rayDir, NE, SW, SE, &t) && t < bestT) {
        bestT = t;
        best.hit = true; best.tileX = tx; best.tileY = ty;
        best.worldPos = rayOrigin + rayDir * t; best.rayT = t;
      }
    }
  }
}

}  // namespace

// Möller-Trumbore ray-triangle intersection (declared in Picker.hpp).
bool rayTriangle(const glm::vec3& orig, const glm::vec3& dir,
                 const glm::vec3& v0,   const glm::vec3& v1, const glm::vec3& v2,
                 float* outT) {
  const glm::vec3 e1 = v1 - v0;
  const glm::vec3 e2 = v2 - v0;
  const glm::vec3 h  = glm::cross(dir, e2);
  const float a = glm::dot(e1, h);
  if (std::abs(a) < 1e-6f) return false;  // ray parallel to triangle plane

  const float f = 1.0f / a;
  const glm::vec3 s = orig - v0;
  const float u = f * glm::dot(s, h);
  if (u < 0.0f || u > 1.0f) return false;

  const glm::vec3 q = glm::cross(s, e1);
  const float v = f * glm::dot(dir, q);
  if (v < 0.0f || u + v > 1.0f) return false;

  const float t = f * glm::dot(e2, q);
  if (t <= 1e-4f) return false;  // hit is behind / at origin
  *outT = t;
  return true;
}

void screenToRay(double pixelX, double pixelY,
                 int screenW,   int screenH,
                 const glm::mat4& viewProj,
                 glm::vec3* outOrigin, glm::vec3* outDir) {
  // Pixel -> NDC. Y is flipped because pixel origin is top-left, NDC is
  // bottom-left (well, bottom-left after the GL convention's flip).
  const float ndcX = (2.0f * static_cast<float>(pixelX)) / static_cast<float>(screenW) - 1.0f;
  const float ndcY = 1.0f - (2.0f * static_cast<float>(pixelY)) / static_cast<float>(screenH);

  const glm::mat4 invVP = glm::inverse(viewProj);

  glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
  nearH /= nearH.w;
  farH  /= farH.w;

  *outOrigin = glm::vec3(nearH);
  *outDir    = glm::normalize(glm::vec3(farH) - glm::vec3(nearH));
}

PickResult pickTile(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                    const std::vector<float>& vh, int W, int H) {
  PickResult best;
  float bestT = std::numeric_limits<float>::max();
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return best;

  // Each tile (tx, ty) shares its 4 corner vertices with the (W+1)*(H+1)
  // grid using Babylon's CreateGround layout: vertex (row, col) sits at
  // world position (col - 0.5, h, H - row - 0.5).
  //
  // For tile (tx, ty):
  //   SW corner = world (tx-0.5, h, ty-0.5) -> vertex row=H-ty,   col=tx
  //   SE corner = world (tx+0.5, h, ty-0.5) -> vertex row=H-ty,   col=tx+1
  //   NW corner = world (tx-0.5, h, ty+0.5) -> vertex row=H-ty-1, col=tx
  //   NE corner = world (tx+0.5, h, ty+0.5) -> vertex row=H-ty-1, col=tx+1
  //
  // TerrainBuilder uses indices (i0, i2, i1) + (i1, i2, i3) where
  //   i0 = (row,   col)     = NW       i2 = (row+1, col)   = SW
  //   i1 = (row,   col+1)   = NE       i3 = (row+1, col+1) = SE
  // so the two triangles per tile are (NW, SW, NE) and (NE, SW, SE).

  pickTilesInRect(rayOrigin, rayDir, vh, W, H, 0, 0, W, H, best, bestT);
  return best;
}

PickResult pickTileChunked(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                           const std::vector<float>& vh, int W, int H,
                           const std::vector<PickRect>& rects) {
  PickResult best;
  float bestT = std::numeric_limits<float>::max();
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return best;

  // Broad phase: slab-test each chunk AABB, keep entries sorted nearest-first.
  std::vector<std::pair<float, const PickRect*>> hits;
  hits.reserve(rects.size());
  for (const auto& r : rects) {
    float tEnter;
    if (rayAabb(rayOrigin, rayDir, r.aabbMin, r.aabbMax, &tEnter))
      hits.emplace_back(tEnter, &r);
  }
  std::sort(hits.begin(), hits.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  // Narrow phase nearest-first; once a hit is closer than the next chunk's
  // entry distance, no farther chunk can beat it.
  for (const auto& [tEnter, r] : hits) {
    if (best.hit && bestT < tEnter) break;
    pickTilesInRect(rayOrigin, rayDir, vh, W, H, r->x0, r->y0, r->w, r->h, best, bestT);
  }
  return best;
}

}  // namespace input
