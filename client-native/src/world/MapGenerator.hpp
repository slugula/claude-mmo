#pragma once

#include "shared/SharedTypes.hpp"

#include <cstdint>

namespace world {

// Generates a procedural WorldMapFile entirely in-memory, with deterministic
// per-vertex heights and per-tile biomes derived from Perlin-noise FBM.
//
// The palette is locked to a fixed set of colors (sourced from p.aseprite —
// see pickPaletteColor() in MapGenerator.cpp). Biomes are assigned by
// elevation band + moisture noise.
//
// baseFreq controls the spatial scale of the FBM: smaller = larger features
// (one big hill across the map), larger = noisier / smaller features.
//
// amplitude scales the vertex-height magnitude. Biome classification is done
// in pre-amplitude space, so raising amplitude makes the same terrain taller
// rather than reshuffling which tiles are water / grass / stone.
shared::WorldMapFile generateMap(int width     = 64,
                                 int height    = 64,
                                 uint32_t seed = 42,
                                 float baseFreq  = 0.04f,
                                 float amplitude = 1.0f);

}  // namespace world
