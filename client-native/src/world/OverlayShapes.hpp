#pragma once

// OSRS-style overlay tile shapes.
//
// Each tile may have a textured overlay painted over part of it using one of
// 12 predefined shapes (0..11). The remaining area shows the underlay (the
// terrain groundColor). Shapes are defined as explicit triangle lists in
// tile-local UV space, where:
//
//   u = 0 is the WEST edge,  u = 1 is the EAST edge
//   v = 0 is the SOUTH edge, v = 1 is the NORTH edge
//
// This matches the world-space mapping used by the terrain / water meshes:
//   worldX = tileX - 0.5 + u
//   worldZ = tileY - 0.5 + v
//
// Nine reference points (a 3x3 grid) name the corners, edge midpoints, and
// centre of the tile:
//
//   TL(0,1)  TM(.5,1)  TR(1,1)        north (v=1)
//   ML(0,.5) MC(.5,.5) MR(1,.5)
//   BL(0,0)  BM(.5,0)  BR(1,0)        south (v=0)
//
// Shape 0 (full tile) is the default and covers the whole tile.
// Complement shapes (6, 11) are the inverse coverage of their counterparts
// (5, 10) so a tile and its complement together tile seamlessly.

#include <array>
#include <vector>

namespace world {

constexpr int kNumOverlayShapes = 12;

// One overlay triangle in tile-local UV space (3 points).
struct ShapeTri {
  float u0, v0;
  float u1, v1;
  float u2, v2;
};

namespace detail {
// Reference points (u, v).
constexpr float TLu = 0.0f, TLv = 1.0f;
constexpr float TMu = 0.5f, TMv = 1.0f;
constexpr float TRu = 1.0f, TRv = 1.0f;
constexpr float MLu = 0.0f, MLv = 0.5f;
constexpr float MCu = 0.5f, MCv = 0.5f;
constexpr float MRu = 1.0f, MRv = 0.5f;
constexpr float BLu = 0.0f, BLv = 0.0f;
constexpr float BMu = 0.5f, BMv = 0.0f;
constexpr float BRu = 1.0f, BRv = 0.0f;
}  // namespace detail

// The 12 shapes. Triangle winding is irrelevant here — the overlay mesh
// is drawn double-sided / unculled, and editor previews fill regardless.
inline const std::array<std::vector<ShapeTri>, kNumOverlayShapes>& overlayShapeTriangles() {
  using namespace detail;
  static const std::array<std::vector<ShapeTri>, kNumOverlayShapes> kShapes = {{
    // 0 — Full tile. Split along the BL<->TR diagonal so that — after the
    // renderer's 180° flip — the two triangles land on the SW<->NE diagonal,
    // matching TerrainBuilder's triangulation exactly. (Splitting the other way
    // makes the flat water/overlay triangles cross a "mountain" terrain ridge
    // along the diagonal, exposing the ground underneath.)
    { {BLu,BLv, BRu,BRv, TRu,TRv}, {BLu,BLv, TRu,TRv, TLu,TLv} },
    // 1 — Diagonal half (NW: TL, TR, BL)
    { {TLu,TLv, TRu,TRv, BLu,BLv} },
    // 2 — Diagonal half (SE: TR, BR, BL)
    { {TRu,TRv, BRu,BRv, BLu,BLv} },
    // 3 — Triangle (TL, TM, BL)
    { {TLu,TLv, TMu,TMv, BLu,BLv} },
    // 4 — Triangle (TR, TM, BL)
    { {TRu,TRv, TMu,TMv, BLu,BLv} },
    // 5 — Quad with centre (TL, TM, MC, BL) — "curved" 4th-vertex look
    { {TLu,TLv, TMu,TMv, MCu,MCv}, {TLu,TLv, MCu,MCv, BLu,BLv} },
    // 6 — Complement of 5 (everything except the TL,TM,MC,BL quad)
    { {TMu,TMv, TRu,TRv, MRu,MRv}, {TMu,TMv, MRu,MRv, MCu,MCv},
      {MCu,MCv, MRu,MRv, BRu,BRv}, {MCu,MCv, BRu,BRv, BMu,BMv},
      {MCu,MCv, BMu,BMv, BLu,BLv} },
    // 7 — Trapezoid (BL, BR, TR, TM)
    { {BLu,BLv, BRu,BRv, TRu,TRv}, {BLu,BLv, TRu,TRv, TMu,TMv} },
    // 8 — Quad (TL, TM, BR, BL)
    { {TLu,TLv, TMu,TMv, BRu,BRv}, {TLu,TLv, BRu,BRv, BLu,BLv} },
    // 9 — Rectangular half (west: TL, TM, BM, BL)
    { {TLu,TLv, TMu,TMv, BMu,BMv}, {TLu,TLv, BMu,BMv, BLu,BLv} },
    // 10 — Small corner triangle (TL, TM, ML)
    { {TLu,TLv, TMu,TMv, MLu,MLv} },
    // 11 — Complement of 10 (everything except the TL,TM,ML corner)
    { {TMu,TMv, TRu,TRv, BRu,BRv}, {TMu,TMv, BRu,BRv, BLu,BLv},
      {TMu,TMv, BLu,BLv, MLu,MLv} },
  }};
  return kShapes;
}

// Rotate a tile-local point (u,v) by `rot` 90° clockwise steps about the tile
// centre (0.5, 0.5). Shared by every consumer (world mesh, minimaps, grid,
// editor preview) so a rotated overlay looks identical in all views.
inline void rotateUV(float& u, float& v, int rot) {
  rot &= 3;
  for (int i = 0; i < rot; ++i) {
    const float nu = v;
    const float nv = 1.0f - u;
    u = nu;
    v = nv;
  }
}

// True if tile-local point (u,v) lies inside the overlay coverage for `shape`
// rotated by `rot` 90° steps. Used by the 2D minimaps to rasterise the shape
// silhouette per sub-pixel. (u,v) are in overlayShapeTriangles() space.
inline bool shapeCoversUV(int shape, float u, float v, int rot = 0) {
  if (shape < 0 || shape >= kNumOverlayShapes) return false;
  auto sign = [](float ax, float ay, float bx, float by, float cx, float cy) {
    return (ax - cx) * (by - cy) - (bx - cx) * (ay - cy);
  };
  for (auto t : overlayShapeTriangles()[static_cast<std::size_t>(shape)]) {
    rotateUV(t.u0, t.v0, rot);
    rotateUV(t.u1, t.v1, rot);
    rotateUV(t.u2, t.v2, rot);
    const float d1 = sign(u, v, t.u0, t.v0, t.u1, t.v1);
    const float d2 = sign(u, v, t.u1, t.v1, t.u2, t.v2);
    const float d3 = sign(u, v, t.u2, t.v2, t.u0, t.v0);
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    if (!(hasNeg && hasPos)) return true;  // inside this triangle
  }
  return false;
}

}  // namespace world
