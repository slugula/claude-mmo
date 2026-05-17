#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace editor {

// 32 OSRS-inspired terrain colours used for the palette swatch grid.
// Each entry is a hex string "#rrggbb" matching the TileData.groundColor format.
// Sourced from the MapGenerator palette + additional hand-picked OSRS tones.
inline constexpr std::array<const char*, 32> kPaletteSwatches = {
  // Greens (mossy, forest, bright)
  "#3a6b1a", "#4a7c2a", "#5a8c3a", "#6a9c4a",
  "#7ec850", "#8ed860", "#2d5010", "#3d6020",
  // Browns / earths
  "#6b4f2a", "#7b5f3a", "#8b6f4a", "#9b7f5a",
  "#a08050", "#704030", "#503020", "#c09060",
  // Greys / stone
  "#808080", "#909090", "#a0a0a0", "#606060",
  "#505050", "#b0b0b0", "#484848", "#c8c8c8",
  // Sandy / dry
  "#c8a864", "#d8b874", "#b89854", "#e8c884",
  // Misc OSRS tones
  "#6a7840", "#7a8850", "#4a5828", "#5a6838",
};

// Convert a hex string "#rrggbb" to normalized float r,g,b (0..1).
inline void hexToRgbf(const char* hex, float& r, float& g, float& b) {
  // Skip leading '#'
  const char* s = (hex && hex[0] == '#') ? hex + 1 : hex;
  unsigned int ri = 0, gi = 0, bi = 0;
  if (s) {
    // NOLINT
    char buf[3] = {};
    buf[0] = s[0]; buf[1] = s[1];
    ri = static_cast<unsigned int>(std::strtoul(buf, nullptr, 16));
    buf[0] = s[2]; buf[1] = s[3];
    gi = static_cast<unsigned int>(std::strtoul(buf, nullptr, 16));
    buf[0] = s[4]; buf[1] = s[5];
    bi = static_cast<unsigned int>(std::strtoul(buf, nullptr, 16));
  }
  r = static_cast<float>(ri) / 255.0f;
  g = static_cast<float>(gi) / 255.0f;
  b = static_cast<float>(bi) / 255.0f;
}

// Convert normalized floats to "#rrggbb".
inline std::string rgbfToHex(float r, float g, float b) {
  const int ri = static_cast<int>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
  const int gi = static_cast<int>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
  const int bi = static_cast<int>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", ri, gi, bi);
  return buf;
}

}  // namespace editor
