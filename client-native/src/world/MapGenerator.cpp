#include "world/MapGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace world {

namespace {

// =====================================================================
// Perlin noise (2D, gradient-based)
// =====================================================================
//
// Ken Perlin's improved 2D noise: 8 unit gradient vectors at each integer
// lattice point (chosen by hash), bilinear interpolation between corner
// dot-products, and the quintic fade(t) = 6t^5 - 15t^4 + 10t^3 for smooth
// first AND second derivatives.

uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u +
               static_cast<uint32_t>(y) * 668265263u +
               seed * 2147483647u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

constexpr std::array<std::array<float, 2>, 8> kGradients = {{
  {{ 1.0f,  0.0f}},
  {{-1.0f,  0.0f}},
  {{ 0.0f,  1.0f}},
  {{ 0.0f, -1.0f}},
  {{ 0.70710678f,  0.70710678f}},
  {{-0.70710678f,  0.70710678f}},
  {{ 0.70710678f, -0.70710678f}},
  {{-0.70710678f, -0.70710678f}},
}};

inline const std::array<float, 2>& gradAt(int x, int y, uint32_t seed) {
  return kGradients[hash2(x, y, seed) & 7];
}

inline float fade(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float perlinRaw(float x, float y, uint32_t seed) {
  const int   x0 = static_cast<int>(std::floor(x));
  const int   y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);

  const auto& g00 = gradAt(x0,     y0,     seed);
  const auto& g10 = gradAt(x0 + 1, y0,     seed);
  const auto& g01 = gradAt(x0,     y0 + 1, seed);
  const auto& g11 = gradAt(x0 + 1, y0 + 1, seed);

  const float d00 = g00[0] * fx         + g00[1] * fy;
  const float d10 = g10[0] * (fx - 1.0f) + g10[1] * fy;
  const float d01 = g01[0] * fx         + g01[1] * (fy - 1.0f);
  const float d11 = g11[0] * (fx - 1.0f) + g11[1] * (fy - 1.0f);

  const float u = fade(fx);
  const float v = fade(fy);
  const float a = d00 + u * (d10 - d00);
  const float b = d01 + u * (d11 - d01);
  return a + v * (b - a);
}

inline float perlin01(float x, float y, uint32_t seed) {
  return perlinRaw(x, y, seed) * 0.70710678f + 0.5f;
}

float fbm(float x, float y, int octaves, float freq, uint32_t seed) {
  float sum   = 0.0f;
  float amp   = 1.0f;
  float total = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum   += perlin01(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 131u) * amp;
    total += amp;
    amp   *= 0.5f;
    freq  *= 2.0f;
  }
  return sum / total;
}

// =====================================================================
// Greens-and-browns palette (sourced from p.aseprite)
// =====================================================================
//
// A 5x4 grid of palette entries. The first axis (rows) is the "moisture"
// noise sample (dry → wet, brown → green); the second axis (columns) is a
// higher-frequency "variant" noise that picks a specific shade within each
// moisture band. Result: every tile gets one of 20 distinct colors with
// patchy local variation across the map.
//
// No water, shore, cliff, stone, mountain, or anything special — every tile
// is grass-class, walkable, no obstacles. Perlin only handles the heights.

constexpr int kMoistureBands = 5;
constexpr int kVariantBands  = 4;

constexpr const char* kPalette[kMoistureBands][kVariantBands] = {
  // m=0 (driest) — dark earth browns
  { "#3D2D0E", "#47380A", "#504425", "#64552F" },
  // m=1 (dry)    — warm browns + tans
  { "#834F21", "#806107", "#827D33", "#A8933D" },
  // m=2 (mid)    — yellow-greens, olives, pale tans
  { "#646A0F", "#51510C", "#707227", "#8A8738" },
  // m=3 (wet)    — light/mid greens
  { "#566E10", "#5F7F20", "#5D801F", "#618122" },
  // m=4 (wettest)— deep forest greens
  { "#395111", "#2F6114", "#1F530F", "#183C0C" },
};

inline const char* pickPaletteColor(float moisture, float variant) {
  const int m = std::clamp(static_cast<int>(moisture * kMoistureBands), 0, kMoistureBands - 1);
  const int v = std::clamp(static_cast<int>(variant  * kVariantBands),  0, kVariantBands  - 1);
  return kPalette[m][v];
}

}  // namespace

// =====================================================================
// Main entry
// =====================================================================

shared::WorldMapFile generateMap(int width, int height, uint32_t seed,
                                 float baseFreq, float amplitude) {
  shared::WorldMapFile map;
  map.width  = width;
  map.height = height;

  const int W = width;
  const int H = height;

  // ---- Per-vertex heights from Perlin FBM, scaled by amplitude ----------
  // (Color is decoupled from height — see below — so changing amplitude
  // makes the same patch of forest taller without changing its hue.)
  const size_t vertCount = static_cast<size_t>((W + 1)) * (H + 1);
  map.vertexHeights.assign(vertCount, 0.0f);
  for (int row = 0; row <= H; ++row) {
    for (int col = 0; col <= W; ++col) {
      const float n = fbm(static_cast<float>(col),
                          static_cast<float>(row),
                          /*octaves*/4, baseFreq, seed);
      map.vertexHeights[row * (W + 1) + col] = (n - 0.5f) * amplitude;
    }
  }

  // ---- Per-tile colors from two independent noise samples ---------------
  // moisture: low frequency, broad green-vs-brown regions
  // variant:  higher frequency, shade variation within each region
  const float moistureFreq = baseFreq * 1.8f;
  const float variantFreq  = baseFreq * 5.0f;
  map.tiles.assign(H, std::vector<shared::TileData>(W));
  for (int ty = 0; ty < H; ++ty) {
    for (int tx = 0; tx < W; ++tx) {
      const float moisture = fbm(static_cast<float>(tx) + 100.0f,
                                 static_cast<float>(ty) + 100.0f,
                                 /*octaves*/3, moistureFreq, seed + 999u);
      const float variant  = fbm(static_cast<float>(tx) + 300.0f,
                                 static_cast<float>(ty) + 300.0f,
                                 /*octaves*/2, variantFreq,  seed + 4242u);

      auto& tile = map.tiles[ty][tx];
      tile.x            = tx;
      tile.y            = ty;
      tile.type         = shared::TileType::grass;
      tile.walkable     = true;
      tile.obstacle     = shared::ObstacleType::none;
      tile.blocksRanged = false;
      tile.height       = 0.0f;  // unused — actual heights live in vertexHeights
      tile.groundColor  = pickPaletteColor(moisture, variant);
    }
  }

  std::fprintf(stdout, "[MapGenerator] %d x %d greens-and-browns map (seed=%u freq=%.3f amp=%.2f)\n",
               W, H, seed, baseFreq, amplitude);
  return map;
}

}  // namespace world
