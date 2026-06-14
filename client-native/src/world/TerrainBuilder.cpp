#include "world/TerrainBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace world {

namespace {

// hex "#rrggbb" -> [r, g, b] in [0..1]
std::array<float, 3> hexToRgb01(const std::string& hex) {
  auto start = (!hex.empty() && hex[0] == '#') ? 1 : 0;
  if (hex.size() < start + 6) return {1.0f, 0.0f, 1.0f};  // magenta = "bad input"
  auto h = [&](int i) { return static_cast<int>(std::stoi(hex.substr(start + i, 2), nullptr, 16)); };
  return { h(0) / 255.0f, h(2) / 255.0f, h(4) / 255.0f };
}

// Resolved height at corner vertex (row, col). Pure: just the vertex-height
// buffer scaled by kMaxTerrainH.
inline float cornerHeight(const std::vector<float>& vertexHeights, int W, int col, int row) {
  return vertexHeights[row * (W + 1) + col] * shared::kMaxTerrainH;
}

}  // namespace

// Builds the tile rect [x0,x0+w) × [y0,y0+h) of the map into a mesh whose
// vertices live in world space. The full-map build is the W×H rect.
//
// Vertex layout matches the original full build (Babylon CreateGround frame):
//   world X = col - 0.5,  world Z = (H - meshRow) - 0.5
// so tile ty sits between mesh rows (H - ty - 1) and (H - ty). The rect's
// local vertex (lr, lc) maps to global mesh row gr = H - y0 - h + lr and
// global column gc = x0 + lc.
TerrainMeshData buildTerrainMeshRect(const shared::WorldMapFile& map,
                                     int x0, int y0, int w, int h) {
  const int W = map.width;
  const int H = map.height;
  const auto& tiles = map.tiles;

  // Water tiles are carved out and replaced by the 3D pool tileset (PoolRenderer),
  // so the terrain quad under each water tile is skipped.
  std::vector<bool> water(static_cast<std::size_t>(W) * H, false);
  for (const auto& ov : map.overlayTiles)
    if (ov.materialId == shared::kWaterMaterialId &&
        ov.tileX >= 0 && ov.tileY >= 0 && ov.tileX < W && ov.tileY < H)
      water[static_cast<std::size_t>(ov.tileY) * W + ov.tileX] = true;
  auto isWaterTile = [&](int tx, int ty) {
    return water[static_cast<std::size_t>(ty) * W + tx];
  };

  // Clamp the rect to the map.
  x0 = std::clamp(x0, 0, W);
  y0 = std::clamp(y0, 0, H);
  w  = std::clamp(w, 0, W - x0);
  h  = std::clamp(h, 0, H - y0);

  const std::vector<float>* vhPtr = &map.vertexHeights;
  std::vector<float> vhFallback;
  if (static_cast<int>(vhPtr->size()) != (W + 1) * (H + 1)) {
    std::fprintf(stderr, "[TerrainBuilder] vertexHeights size mismatch: got %zu, expected %d\n",
                 vhPtr->size(), (W + 1) * (H + 1));
    vhFallback.assign(static_cast<size_t>((W + 1)) * (H + 1), 0.0f);
    vhPtr = &vhFallback;
  }
  const auto& vh = *vhPtr;

  TerrainMeshData mesh;
  mesh.width  = w;
  mesh.height = h;
  if (w == 0 || h == 0) return mesh;

  const size_t vcount = static_cast<size_t>((w + 1)) * (h + 1);
  mesh.positions.resize(vcount * 3);
  mesh.colors.resize(vcount * 4);
  mesh.normals.resize(vcount * 3);
  mesh.triangleIndices.reserve(static_cast<size_t>(w) * h * 6);
  mesh.lineIndices.reserve(
      (static_cast<size_t>(h + 1) * w + static_cast<size_t>(w + 1) * h) * 2);

  const int grBase = H - y0 - h;  // global mesh row of local row 0

  // ---- Vertices: position + neighbor-averaged color -----------------------
  // Each corner sits at the boundary of up to 4 tiles; its color averages
  // those neighbors' groundColors (sampled from the FULL map so rect borders
  // shade exactly like the monolithic build). Void filler tiles are excluded
  // so real chunk borders don't darken toward unrendered cells.
  for (int lr = 0; lr <= h; ++lr) {
    const int gr = grBase + lr;
    for (int lc = 0; lc <= w; ++lc) {
      const int gc = x0 + lc;
      const size_t v = static_cast<size_t>(lr) * (w + 1) + lc;
      const float y  = cornerHeight(vh, W, gc, gr);
      mesh.positions[v * 3 + 0] = static_cast<float>(gc) - 0.5f;
      mesh.positions[v * 3 + 1] = y;
      mesh.positions[v * 3 + 2] = static_cast<float>(H - gr) - 0.5f;

      float r = 0.0f, g = 0.0f, b = 0.0f;
      int   count = 0;
      for (int tx : {gc - 1, gc}) {
        for (int ty : {H - gr - 1, H - gr}) {
          if (tx < 0 || tx >= W || ty < 0 || ty >= H) continue;
          if (tiles[ty][tx].isVoid) continue;
          auto [tr, tg, tb] = hexToRgb01(tiles[ty][tx].groundColor);
          r += tr; g += tg; b += tb; ++count;
        }
      }
      if (count > 0) { r /= count; g /= count; b /= count; }
      mesh.colors[v * 4 + 0] = r;
      mesh.colors[v * 4 + 1] = g;
      mesh.colors[v * 4 + 2] = b;

      // ---- AO: concavity darkening packed into color alpha ----------------
      // Sum the excess height of all 8 neighbor vertices above this one,
      // sampled from the full map's height field.
      float aoSum = 0.0f;
      for (int dr : {-1, 0, 1}) {
        for (int dc : {-1, 0, 1}) {
          if (dr == 0 && dc == 0) continue;
          const int nr = std::clamp(gr + dr, 0, H);
          const int nc = std::clamp(gc + dc, 0, W);
          const float diff = cornerHeight(vh, W, nc, nr) - y;
          if (diff > 0.0f) aoSum += diff;
        }
      }
      const float aoNorm   = 8.0f * static_cast<float>(shared::kMaxTerrainH) * 0.35f;
      mesh.colors[v * 4 + 3] = std::clamp(aoSum / aoNorm, 0.0f, 1.0f);

      // ---- Normal via central difference on the full height field ---------
      const int colE = std::min(gc + 1, W);
      const int colW = std::max(gc - 1, 0);
      const int rowS = std::min(gr + 1, H);
      const int rowN = std::max(gr - 1, 0);
      const float hE = cornerHeight(vh, W, colE, gr);
      const float hW = cornerHeight(vh, W, colW, gr);
      const float hN = cornerHeight(vh, W, gc,  rowN);
      const float hS = cornerHeight(vh, W, gc,  rowS);
      const float dx = (hE - hW) / static_cast<float>(std::max(colE - colW, 1));
      const float dz = (hN - hS) / static_cast<float>(std::max(rowS - rowN, 1));
      float nx = -dx, ny = 1.0f, nz = -dz;
      const float invLen = 1.0f / std::sqrt(nx*nx + ny*ny + nz*nz);
      mesh.normals[v * 3 + 0] = nx * invLen;
      mesh.normals[v * 3 + 1] = ny * invLen;
      mesh.normals[v * 3 + 2] = nz * invLen;
    }
  }

  // ---- Triangle indices: two triangles per tile, BL→TR diagonal -----------
  // Void filler tiles emit no geometry, so unassigned world cells render as
  // nothing (sky) instead of dark ground.
  for (int lr = 0; lr < h; ++lr) {
    const int ty = H - (grBase + lr) - 1;       // global tile row of this quad
    for (int lc = 0; lc < w; ++lc) {
      if (tiles[ty][x0 + lc].isVoid) continue;
      if (isWaterTile(x0 + lc, ty)) continue;   // carved out — pool tileset draws here
      const uint32_t i0 = static_cast<uint32_t>(lr * (w + 1) + lc);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + (w + 1);
      const uint32_t i3 = i2 + 1;
      mesh.triangleIndices.insert(mesh.triangleIndices.end(),
                                  { i0, i2, i1,   i1, i2, i3 });
    }
  }

  // ---- Line indices: tile perimeter edges, deduplicated -------------------
  for (int lr = 0; lr <= h; ++lr) {
    for (int lc = 0; lc < w; ++lc) {
      const uint32_t a = static_cast<uint32_t>(lr * (w + 1) + lc);
      mesh.lineIndices.push_back(a);
      mesh.lineIndices.push_back(a + 1);
    }
  }
  for (int lr = 0; lr < h; ++lr) {
    for (int lc = 0; lc <= w; ++lc) {
      const uint32_t a = static_cast<uint32_t>(lr * (w + 1) + lc);
      mesh.lineIndices.push_back(a);
      mesh.lineIndices.push_back(a + (w + 1));
    }
  }

  return mesh;
}

TerrainMeshData buildTerrainMesh(const shared::WorldMapFile& map) {
  return buildTerrainMeshRect(map, 0, 0, map.width, map.height);
}

}  // namespace world
