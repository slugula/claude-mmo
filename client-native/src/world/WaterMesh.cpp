#include "world/WaterMesh.hpp"

#include "world/OverlayShapes.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace world {

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

  const int W = map.width, H = map.height;

  // Water tiles are sourced from the overlay layer (materialId == water). Each
  // carries a shape (0..11) so water edges can be triangular/partial instead of
  // always full-tile. Legacy maps' waterTiles[] are migrated to overlayTiles on
  // load, so we only need to read overlayTiles here.
  struct WaterCell { int tx, ty, shape, rotation; };
  std::vector<WaterCell> waterCells;
  waterCells.reserve(map.overlayTiles.size());
  for (const auto& ov : map.overlayTiles) {
    if (ov.materialId != shared::kWaterMaterialId) continue;
    if (ov.tileX < 0 || ov.tileY < 0 || ov.tileX >= W || ov.tileY >= H) continue;
    const int shape = (ov.shape >= 0 && ov.shape < kNumOverlayShapes) ? ov.shape : 0;
    waterCells.push_back({ ov.tileX, ov.tileY, shape, ov.rotation });
  }
  if (waterCells.empty()) return;

  // Build O(1) lookup grid instead of iterating the vector for every tile query.
  std::vector<std::vector<bool>> waterGrid(
      static_cast<std::size_t>(H),
      std::vector<bool>(static_cast<std::size_t>(W), false));
  for (const auto& wc : waterCells)
    waterGrid[static_cast<std::size_t>(wc.ty)][static_cast<std::size_t>(wc.tx)] = true;

  auto isWater = [&](int x, int y) -> bool {
    if (x < 0 || y < 0 || x >= W || y >= H) return false;
    return waterGrid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
  };

  std::vector<WaterVertex>  verts;
  std::vector<unsigned int> indices;
  verts.reserve(waterCells.size() * 6);
  indices.reserve(waterCells.size() * 6);

  // --- Flush-draped water surface ------------------------------------------
  // Water now sits FLUSH on the terrain, draped onto the actual per-tile vertex
  // heights like any other overlay (no carving, no single global plane). This
  // means water is never buried under higher ground and never deforms terrain.
  // `waterOffset` becomes a small "raise above terrain" amount (default 0);
  // depth/shore appearance is the shader's job, not geometry's.
  //
  // Vertex indexing: row 0 = south edge (ty=H), row H = north edge (ty=0).
  // For tile (tx, ty):  SW = (tx,   H-ty),   SE = (tx+1, H-ty)
  //                     NW = (tx,   H-ty-1), NE = (tx+1, H-ty-1)
  const auto& vh = map.vertexHeights;
  const bool  vhValid = (static_cast<int>(vh.size()) == (W + 1) * (H + 1));

  auto cornerH = [&](int vc, int vr) -> float {
    if (!vhValid || vc < 0 || vc > W || vr < 0 || vr > H) return 0.f;
    return vh[static_cast<std::size_t>(vr * (W + 1) + vc)] * shared::kMaxTerrainH;
  };
  // Terrain-exact triangulated height so water seats flush on the terrain.
  // MUST match TerrainBuilder's diagonal, which runs SW<->NE (the u==v line):
  // terrain tris are {SW,SE,NE} for u>=v and {SW,NE,NW} for u<v. Using the
  // wrong (NW<->SE) diagonal makes the surface cross the terrain on sloped
  // tiles, so the ground pokes through.
  auto terrainHeightAt = [](float u, float v,
                            float hSW, float hSE, float hNW, float hNE) -> float {
    if (u >= v) return hSW + u * (hSE - hSW) + v * (hNE - hSE);  // SW,SE,NE
    return hSW + v * (hNW - hSW) + u * (hNE - hNW);              // SW,NE,NW
  };
  // Water now sits inside the carved 3D pool tileset (terrain under water tiles
  // is removed), so the surface sits SLIGHTLY BELOW the terrain rim rather than
  // flush on top — the pool walls/floor are visible around/under it.
  constexpr float kPoolSurfaceDrop = 0.12f;

  const auto& shapes = overlayShapeTriangles();

  for (const auto& wc : waterCells) {
    const int tx = wc.tx, ty = wc.ty;

    const float hSW = cornerH(tx,     H - ty);
    const float hSE = cornerH(tx + 1, H - ty);
    const float hNW = cornerH(tx,     H - ty - 1);
    const float hNE = cornerH(tx + 1, H - ty - 1);

    // Per-corner shore_weight: fraction of the 4 tiles sharing that corner that
    // are NOT water (out-of-bounds counts as non-water). Indexed by tile-local
    // (u,v): SW(0,0), SE(1,0), NW(0,1), NE(1,1). For partial shapes we bilerp
    // these four values at each emitted vertex so foam stays continuous.
    auto cornerShore = [&](int adj[4][2]) -> float {
      int nonWater = 0;
      for (int k = 0; k < 4; ++k)
        if (!isWater(adj[k][0], adj[k][1])) ++nonWater;
      return static_cast<float>(nonWater) / 4.0f;
    };
    int swAdj[4][2] = {{tx-1,ty-1},{tx,ty-1},{tx-1,ty},{tx,ty}};
    int seAdj[4][2] = {{tx,ty-1},{tx+1,ty-1},{tx,ty},{tx+1,ty}};
    int nwAdj[4][2] = {{tx-1,ty},{tx,ty},{tx-1,ty+1},{tx,ty+1}};
    int neAdj[4][2] = {{tx,ty},{tx+1,ty},{tx,ty+1},{tx+1,ty+1}};
    const float sSW = cornerShore(swAdj);
    const float sSE = cornerShore(seAdj);
    const float sNW = cornerShore(nwAdj);
    const float sNE = cornerShore(neAdj);

    auto emit = [&](float u, float v) {
      // Authored rotation, then the 180° flip to match the editor shape preview
      // (see OverlayRenderer::rebuild for the rationale).
      rotateUV(u, v, wc.rotation);
      const float uu = 1.0f - u;
      const float vv = 1.0f - v;
      const float wx = static_cast<float>(tx) - 0.5f + uu;
      const float wz = static_cast<float>(ty) - 0.5f + vv;
      const float shore = (1.f - uu) * (1.f - vv) * sSW + uu * (1.f - vv) * sSE +
                          (1.f - uu) * vv * sNW + uu * vv * sNE;
      const float wy = terrainHeightAt(uu, vv, hSW, hSE, hNW, hNE)
                       - kPoolSurfaceDrop + waterOffset;
      WaterVertex vert;
      vert.pos          = { wx, wy, wz };
      vert.uv           = { wx, wz };
      vert.normal       = { 0.0f, 1.0f, 0.0f };
      vert.shore_weight = shore;
      indices.push_back(static_cast<unsigned int>(verts.size()));
      verts.push_back(vert);
    };

    for (const auto& t : shapes[static_cast<std::size_t>(wc.shape)]) {
      emit(t.u0, t.v0);
      emit(t.u1, t.v1);
      emit(t.u2, t.v2);
    }
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
