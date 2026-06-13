#pragma once

#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <vector>

namespace editor {

// Rasterise a map's base layer (terrain groundColor + obstacle tints + shaped
// overlays incl. water + white wall/pillar lines) into an RGBA8 buffer at
// `pxPerTile` resolution. Texture-native orientation: +tileX → +U (right),
// tile (0,0) at the top-left. This is the same raster the minimap windows use,
// so callers (minimap, World-view thumbnails) all match 1:1. NPC/spawn markers
// are NOT included — add those per use site.
void rasterMapBase(const shared::WorldMapFile& map, int pxPerTile,
                   std::vector<uint8_t>& outBuf, int& outW, int& outH);

// Generates and manages the OSRS-style 4×4 px-per-tile minimap texture.
// The texture is 256×256 for a 64×64 map (scalable for other sizes).
// Reconstructed fully whenever the map changes.
class MinimapRenderer {
public:
  MinimapRenderer() = default;
  ~MinimapRenderer();

  MinimapRenderer(const MinimapRenderer&)            = delete;
  MinimapRenderer& operator=(const MinimapRenderer&) = delete;

  // Upload an initial black texture sized for `mapW × mapH`.
  void init(int mapW, int mapH);

  // Rebuild the CPU buffer from the current map state and re-upload.
  void rebuild(const shared::WorldMapFile& map,
               const std::vector<shared::NpcSpawn>& npcs);

  // Release GL resources.
  void destroy();

  GLuint texture() const { return tex_; }
  int texW() const { return texW_; }
  int texH() const { return texH_; }

private:
  GLuint tex_  = 0;
  int    texW_ = 0;
  int    texH_ = 0;
  std::vector<uint8_t> buf_;  // RGBA, texW_ × texH_

  void setPixel(int px, int py, uint8_t r, uint8_t g, uint8_t b);
  void fillRect(int px, int py, int pw, int ph, uint8_t r, uint8_t g, uint8_t b);
};

}  // namespace editor
