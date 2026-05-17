#pragma once

#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <vector>

namespace editor {

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
