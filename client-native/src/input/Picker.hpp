#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace input {

struct PickResult {
  bool      hit      = false;
  int       tileX    = 0;
  int       tileY    = 0;
  glm::vec3 worldPos = glm::vec3(0.0f);
  float     rayT     = 0.0f;  // distance along the ray (useful for hit ordering)
};

// Möller-Trumbore ray-triangle intersection. Writes the parametric distance t
// along the ray on hit (in front of the origin). Reusable for narrow-phase
// mesh picking (transform the ray into model-local space first).
bool rayTriangle(const glm::vec3& orig, const glm::vec3& dir,
                 const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                 float* outT);

// Build a world-space ray from a screen pixel. The pixel coordinate system is
// (x: right, y: down) — i.e. matches GLFW's cursor convention with (0,0) at
// the window's top-left. Returns origin + a normalized direction.
void screenToRay(double pixelX, double pixelY,
                 int screenW,   int screenH,
                 const glm::mat4& viewProj,
                 glm::vec3* outOrigin, glm::vec3* outDir);

// Brute-force ray-vs-heightfield pick. Iterates every tile (W*H), runs
// Möller-Trumbore against both of the tile's triangles (matching the
// TerrainBuilder topology: 2 triangles per tile, BL→TR diagonal), and
// returns the earliest hit. O(W*H); for 64x64 that's 4096 tile tests per
// pick, which is microseconds on modern CPUs.
//
// vertexHeights is the same raw-value array stored in WorldMapFile —
// values are scaled by shared::kMaxTerrainH internally.
PickResult pickTile(const glm::vec3&          rayOrigin,
                    const glm::vec3&          rayDir,
                    const std::vector<float>& vertexHeights,
                    int W, int H);

// Chunk-culled variant for large (multi-chunk) worlds: slab-tests each
// candidate rect's AABB, visits surviving rects nearest-first, runs the
// heightfield test only inside the rect, and early-outs once a hit is closer
// than the next rect's entry distance. With 64-tile render chunks this keeps
// per-pick cost ~O(visible chunk count + 64*64) instead of O(W*H).
struct PickRect {
  int x0 = 0, y0 = 0, w = 0, h = 0;     // tile rect in world tile coords
  glm::vec3 aabbMin{0.0f}, aabbMax{0.0f};
};
PickResult pickTileChunked(const glm::vec3&             rayOrigin,
                           const glm::vec3&             rayDir,
                           const std::vector<float>&    vertexHeights,
                           int W, int H,
                           const std::vector<PickRect>& rects);

}  // namespace input
