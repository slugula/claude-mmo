#pragma once

#include "shared/SharedTypes.hpp"

#include <cstdint>
#include <vector>

namespace world {

// CPU-side terrain mesh data.
//
// OSRS-faithful: pure vertex colors, no textures. Each tile is a quad split
// into two triangles along the BL→TR diagonal, with corner vertices welded
// across up to 4 adjacent tiles. Colors are averaged from neighboring tile
// groundColors, darkened by obstacle-AO, and baked into a single per-vertex
// RGBA value. The GPU Gouraud-interpolates them across each triangle.
//
// (Phase 7 will add HSL-space averaging + palette quantization in the
// fragment shader — that's what gives OSRS its banded, flat-shaded look.
// Until then, mid-tile gradients will look a bit soft, which is expected.)
//
// Vertex layout matches Babylon's MeshBuilder.CreateGround:
//   vertex (row, col) at world position ( col - 0.5,  height,  H - row - 0.5 )
//   vertex index      = row * (W+1) + col
//
// Each vertex carries:
//   - position  (vec3)
//   - color     (vec4)  — neighbor-averaged RGB * AO, alpha = 1
//
// Two index buffers are produced:
//   - triangleIndices: filled rendering, GL_TRIANGLES, W*H*6 indices
//   - lineIndices:     wireframe overlay, GL_LINES, tile perimeters only,
//                      deduplicated so each shared edge appears once.
struct TerrainMeshData {
  std::vector<float>    positions;       // (W+1)*(H+1) * 3 floats
  std::vector<float>    colors;          // (W+1)*(H+1) * 4 floats (RGBA)
  std::vector<uint32_t> triangleIndices; // W*H*6 indices (2 triangles per tile)
  std::vector<uint32_t> lineIndices;     // ((H+1)*W + (W+1)*H) * 2 indices
  int                   width  = 0;
  int                   height = 0;
};

TerrainMeshData buildTerrainMesh(const shared::WorldMapFile& map);

}  // namespace world
