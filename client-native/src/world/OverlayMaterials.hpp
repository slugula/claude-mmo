#pragma once

// Overlay material registry.
//
// An overlay tile references a material by index (materialId). Index 0 is the
// reserved "none" sentinel. Index 3 is water (shared::kWaterMaterialId) — water
// overlays are rendered by WaterRenderer (animated), all other materials by
// OverlayRenderer (static textured surfaces: paths, floors, etc.).
//
// Textures are loaded into a single GL_TEXTURE_2D_ARRAY at startup; the array
// layer index equals the materialId, so the fragment shader samples
// texture(u_texArray, vec3(uv, materialId)).
//
// All textures must share the same resolution (kOverlayTexSize). To add a
// material: append an entry here and drop a matching PNG under
// assets/textures/terrain/. Keep "water" at index 3 to match
// shared::kWaterMaterialId.

#include "shared/SharedTypes.hpp"

#include <string>
#include <vector>

namespace world {

constexpr int kOverlayTexSize = 128;  // all overlay textures are 128x128

struct OverlayMaterial {
  std::string   name;         // editor display name
  std::string   texturePath;  // relative to the executable (assets copied post-build)
  float         uvScale;      // tiling repeats per tile (1 = one texture per tile)
  unsigned char mr, mg, mb;   // representative colour for 2D grid + minimap
};

// Index 0 = none (reserved); index 3 = water (shared::kWaterMaterialId).
inline const std::vector<OverlayMaterial>& overlayMaterials() {
  static const std::vector<OverlayMaterial> kMaterials = {
    { "none",       "",                                        1.0f,  0,   0,   0 },  // 0
    { "dirt_path",  "assets/textures/terrain/dirt_path.png",   1.0f, 134, 100,  64 },  // 1
    { "stone_path", "assets/textures/terrain/stone_path.png",  1.0f, 120, 120, 124 },  // 2
    { "water",      "assets/textures/terrain/water_still.png", 1.0f,  38, 102, 204 },  // 3 (animated via WaterRenderer)
    { "gravel",     "assets/textures/terrain/gravel.png",      1.0f, 110, 104,  92 },  // 4
    { "sand",       "assets/textures/terrain/sand.png",        1.0f, 206, 188, 130 },  // 5
  };
  return kMaterials;
}

static_assert(true);  // (kWaterMaterialId validated at runtime in OverlayRenderer)

}  // namespace world
