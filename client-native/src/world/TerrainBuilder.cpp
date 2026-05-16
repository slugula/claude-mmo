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
// buffer scaled by kMaxTerrainH. (The earlier "all-water → sink below"
// special case has been removed since the simplified generator no longer
// produces water tiles.)
inline float computeCornerHeight(
    const std::vector<float>& vertexHeights,
    int W, int /*H*/, int col, int row) {
  return vertexHeights[row * (W + 1) + col] * shared::kMaxTerrainH;
}

}  // namespace

TerrainMeshData buildTerrainMesh(const shared::WorldMapFile& map) {
  const int W = map.width;
  const int H = map.height;
  const auto& tiles = map.tiles;

  std::vector<float> vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) {
    std::fprintf(stderr, "[TerrainBuilder] vertexHeights size mismatch: got %zu, expected %d\n",
                 vh.size(), (W + 1) * (H + 1));
    vh.assign(static_cast<size_t>((W + 1)) * (H + 1), 0.0f);
  }

  TerrainMeshData mesh;
  mesh.width  = W;
  mesh.height = H;
  const size_t vcount = static_cast<size_t>((W + 1)) * (H + 1);
  mesh.positions.resize(vcount * 3);
  mesh.colors.resize(vcount * 4);
  mesh.normals.resize(vcount * 3);
  mesh.triangleIndices.reserve(static_cast<size_t>(W) * H * 6);
  mesh.lineIndices.reserve(
      (static_cast<size_t>(H + 1) * W + static_cast<size_t>(W + 1) * H) * 2);

  // ---- Vertices: position + neighbor-averaged color -----------------------
  // Each corner sits at the boundary of up to 4 tiles. The corner's color is
  // simply the average of those neighbors' groundColors. Averaging is in
  // linear RGB here; Phase 7 quantizes the GPU-interpolated result back to a
  // discrete HSL palette per fragment.
  //
  // (Earlier versions skipped water tiles in this average and applied an
  // obstacle-AO darkening. Neither is needed now — the simplified generator
  // produces only walkable grass tiles with no obstacles.)
  for (int row = 0; row <= H; ++row) {
    for (int col = 0; col <= W; ++col) {
      const size_t v = row * (W + 1) + col;
      const float y  = computeCornerHeight(vh, W, H, col, row);
      mesh.positions[v * 3 + 0] = static_cast<float>(col) - 0.5f;
      mesh.positions[v * 3 + 1] = y;
      mesh.positions[v * 3 + 2] = static_cast<float>(H - row) - 0.5f;

      float r = 0.0f, g = 0.0f, b = 0.0f;
      int   count = 0;
      for (int tx : {col - 1, col}) {
        for (int ty : {H - row - 1, H - row}) {
          if (tx < 0 || tx >= W || ty < 0 || ty >= H) continue;
          auto [tr, tg, tb] = hexToRgb01(tiles[ty][tx].groundColor);
          r += tr; g += tg; b += tb; ++count;
        }
      }
      if (count > 0) { r /= count; g /= count; b /= count; }
      mesh.colors[v * 4 + 0] = r;
      mesh.colors[v * 4 + 1] = g;
      mesh.colors[v * 4 + 2] = b;
      mesh.colors[v * 4 + 3] = 1.0f;

      // ---- Normal via central difference on the height field ---------------
      //
      // World layout:  +X = east  (col increases), +Z = north (row decreases).
      // Edge corners clamp the neighbor index so the boundary normals don't
      // shoot off — a one-sided difference works fine at the edge.
      const int colE = std::min(col + 1, W);
      const int colW = std::max(col - 1, 0);
      const int rowS = std::min(row + 1, H);   // larger row = smaller Z
      const int rowN = std::max(row - 1, 0);
      const float hE = computeCornerHeight(vh, W, H, colE, row);
      const float hW = computeCornerHeight(vh, W, H, colW, row);
      const float hN = computeCornerHeight(vh, W, H, col,  rowN);  // +Z
      const float hS = computeCornerHeight(vh, W, H, col,  rowS);  // -Z
      const float dx = (hE - hW) / static_cast<float>(std::max(colE - colW, 1));
      const float dz = (hN - hS) / static_cast<float>(std::max(rowS - rowN, 1));
      // n = normalize( cross( (1, dx, 0), (0, dz, 1) ) ) = normalize(-dx, 1, -dz)
      float nx = -dx;
      float ny =  1.0f;
      float nz = -dz;
      const float invLen = 1.0f / std::sqrt(nx*nx + ny*ny + nz*nz);
      mesh.normals[v * 3 + 0] = nx * invLen;
      mesh.normals[v * 3 + 1] = ny * invLen;
      mesh.normals[v * 3 + 2] = nz * invLen;
    }
  }

  // ---- Triangle indices: two triangles per tile, BL→TR diagonal -----------
  for (int row = 0; row < H; ++row) {
    for (int col = 0; col < W; ++col) {
      const uint32_t i0 = static_cast<uint32_t>(row * (W + 1) + col);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + (W + 1);
      const uint32_t i3 = i2 + 1;
      mesh.triangleIndices.insert(mesh.triangleIndices.end(),
                                  { i0, i2, i1,   i1, i2, i3 });
    }
  }

  // ---- Line indices: tile perimeter edges, deduplicated -------------------
  for (int row = 0; row <= H; ++row) {
    for (int col = 0; col < W; ++col) {
      const uint32_t a = static_cast<uint32_t>(row * (W + 1) + col);
      mesh.lineIndices.push_back(a);
      mesh.lineIndices.push_back(a + 1);
    }
  }
  for (int row = 0; row < H; ++row) {
    for (int col = 0; col <= W; ++col) {
      const uint32_t a = static_cast<uint32_t>(row * (W + 1) + col);
      mesh.lineIndices.push_back(a);
      mesh.lineIndices.push_back(a + (W + 1));
    }
  }

  return mesh;
}

}  // namespace world
