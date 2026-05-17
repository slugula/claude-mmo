#include "world/WaterMesh.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace world {

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

static float tileWorldY(const shared::WorldMapFile& map, int tx, int ty) {
  const int W = map.width, H = map.height;
  if (W <= 0 || H <= 0 || tx < 0 || ty < 0 || tx >= W || ty >= H) return 0.0f;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return 0.0f;
  const float sw = vh[static_cast<std::size_t>((H - ty)     * (W + 1) + tx)]     * shared::kMaxTerrainH;
  const float se = vh[static_cast<std::size_t>((H - ty)     * (W + 1) + tx + 1)] * shared::kMaxTerrainH;
  const float nw = vh[static_cast<std::size_t>((H - ty - 1) * (W + 1) + tx)]     * shared::kMaxTerrainH;
  const float ne = vh[static_cast<std::size_t>((H - ty - 1) * (W + 1) + tx + 1)] * shared::kMaxTerrainH;
  return (sw + se + nw + ne) * 0.25f;
}

// ---------------------------------------------------------------------------
void WaterMesh::destroy() {
  if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
  if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
  if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
  indexCount_ = 0;
}

// ---------------------------------------------------------------------------
void WaterMesh::build(const shared::WorldMapFile& map, float waterOffset) {
  destroy();
  if (map.waterTiles.empty()) return;

  const int W = map.width, H = map.height;

  // Build O(1) lookup grid instead of iterating the vector for every tile query.
  std::vector<std::vector<bool>> waterGrid(
      static_cast<std::size_t>(H),
      std::vector<bool>(static_cast<std::size_t>(W), false));
  for (const auto& wt : map.waterTiles) {
    if (wt.tileX >= 0 && wt.tileX < W && wt.tileY >= 0 && wt.tileY < H)
      waterGrid[static_cast<std::size_t>(wt.tileY)][static_cast<std::size_t>(wt.tileX)] = true;
  }

  auto isWater = [&](int x, int y) -> bool {
    if (x < 0 || y < 0 || x >= W || y >= H) return false;
    return waterGrid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
  };

  std::vector<WaterVertex>  verts;
  std::vector<unsigned int> indices;
  verts.reserve(map.waterTiles.size() * 4);
  indices.reserve(map.waterTiles.size() * 6);

  // --- Single global water Y ------------------------------------------------
  // Compute ONE shared surface height from the average of ALL non-water border
  // tile heights across the entire water body.  This prevents per-tile height
  // differences that produce a stepped / diamond-shaped look.
  float hSum = 0.0f;
  int   hCnt = 0;
  for (const auto& wt : map.waterTiles) {
    if (wt.tileX < 0 || wt.tileY < 0 || wt.tileX >= W || wt.tileY >= H) continue;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        const int nx = wt.tileX + dx, ny = wt.tileY + dy;
        if (nx >= 0 && ny >= 0 && nx < W && ny < H && !isWater(nx, ny)) {
          hSum += tileWorldY(map, nx, ny);
          ++hCnt;
        }
      }
    }
  }
  // Fallback when all tiles are interior (no terrain neighbours): use the
  // average height of the water tiles themselves.
  if (hCnt == 0) {
    for (const auto& wt : map.waterTiles) {
      if (wt.tileX >= 0 && wt.tileY >= 0 && wt.tileX < W && wt.tileY < H) {
        hSum += tileWorldY(map, wt.tileX, wt.tileY);
        ++hCnt;
      }
    }
  }
  const float globalWaterY = (hCnt > 0 ? hSum / static_cast<float>(hCnt) : 0.0f)
                             - waterOffset;

  for (const auto& wt : map.waterTiles) {
    const int tx = wt.tileX, ty = wt.tileY;
    if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;

    const float waterY = globalWaterY;

    const float cx = static_cast<float>(tx);
    const float cz = static_cast<float>(ty);

    // Per-corner shore_weight: fraction of the 4 tiles sharing that corner
    // that are NOT water.  Out-of-bounds tiles count as non-water.
    //
    // Corner layout (relative xOff, zOff) and the 4 adjacent tile indices:
    //   SW (-0.5,-0.5): (tx-1,ty-1), (tx,ty-1), (tx-1,ty), (tx,ty)
    //   SE (+0.5,-0.5): (tx,ty-1),   (tx+1,ty-1),(tx,ty),   (tx+1,ty)
    //   NE (+0.5,+0.5): (tx,ty),     (tx+1,ty),  (tx,ty+1), (tx+1,ty+1)
    //   NW (-0.5,+0.5): (tx-1,ty),   (tx,ty),    (tx-1,ty+1),(tx,ty+1)
    struct CornerDef { float xOff, zOff; int adj[4][2]; };
    const CornerDef corners[4] = {
      { -0.5f, -0.5f, {{tx-1,ty-1},{tx,ty-1},{tx-1,ty},{tx,ty}} },
      { +0.5f, -0.5f, {{tx,ty-1},{tx+1,ty-1},{tx,ty},{tx+1,ty}} },
      { +0.5f, +0.5f, {{tx,ty},{tx+1,ty},{tx,ty+1},{tx+1,ty+1}} },
      { -0.5f, +0.5f, {{tx-1,ty},{tx,ty},{tx-1,ty+1},{tx,ty+1}} },
    };

    const auto base = static_cast<unsigned int>(verts.size());
    for (int ci = 0; ci < 4; ++ci) {
      const auto& c = corners[ci];
      int nonWater = 0;
      for (int k = 0; k < 4; ++k) {
        if (!isWater(c.adj[k][0], c.adj[k][1])) ++nonWater;
      }
      WaterVertex v;
      v.pos          = { cx + c.xOff, waterY, cz + c.zOff };
      v.uv           = { cx + c.xOff, cz + c.zOff };
      v.normal       = { 0.0f, 1.0f, 0.0f };
      v.shore_weight = static_cast<float>(nonWater) / 4.0f;
      verts.push_back(v);
    }
    // CCW winding: SW(0), SE(1), NE(2), NW(3)
    indices.insert(indices.end(), { base, base+1, base+2, base, base+2, base+3 });
  }

  if (verts.empty()) return;

  // Upload
  glCreateVertexArrays(1, &vao_);
  glCreateBuffers(1, &vbo_);
  glCreateBuffers(1, &ebo_);

  glNamedBufferStorage(vbo_,
    static_cast<GLsizeiptr>(verts.size()   * sizeof(WaterVertex)),
    verts.data(), 0);
  glNamedBufferStorage(ebo_,
    static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
    indices.data(), 0);

  glVertexArrayVertexBuffer (vao_, 0, vbo_, 0, static_cast<GLsizei>(sizeof(WaterVertex)));
  glVertexArrayElementBuffer(vao_, ebo_);

  // layout(location=0) pos
  glEnableVertexArrayAttrib (vao_, 0);
  glVertexArrayAttribFormat (vao_, 0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(WaterVertex, pos)));
  glVertexArrayAttribBinding(vao_, 0, 0);

  // layout(location=1) uv
  glEnableVertexArrayAttrib (vao_, 1);
  glVertexArrayAttribFormat (vao_, 1, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(WaterVertex, uv)));
  glVertexArrayAttribBinding(vao_, 1, 0);

  // layout(location=2) normal
  glEnableVertexArrayAttrib (vao_, 2);
  glVertexArrayAttribFormat (vao_, 2, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(WaterVertex, normal)));
  glVertexArrayAttribBinding(vao_, 2, 0);

  // layout(location=3) shore_weight
  glEnableVertexArrayAttrib (vao_, 3);
  glVertexArrayAttribFormat (vao_, 3, 1, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(WaterVertex, shore_weight)));
  glVertexArrayAttribBinding(vao_, 3, 0);

  indexCount_ = static_cast<int>(indices.size());
}

// ---------------------------------------------------------------------------
void WaterMesh::draw() const {
  if (!vao_ || indexCount_ == 0) return;
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

}  // namespace world
