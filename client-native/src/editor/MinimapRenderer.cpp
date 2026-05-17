#include "editor/MinimapRenderer.hpp"
#include "editor/EditorPalette.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace editor {

// Pixels per tile — matches OSRS minimap resolution exactly.
static constexpr int kPxPerTile = 4;

// Darkness factor for obstacle overlay (0=black, 1=unchanged).
static constexpr float kObstacleDarken = 0.55f;

MinimapRenderer::~MinimapRenderer() { destroy(); }

void MinimapRenderer::destroy() {
  if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
}

void MinimapRenderer::init(int mapW, int mapH) {
  destroy();
  texW_ = mapW * kPxPerTile;
  texH_ = mapH * kPxPerTile;
  buf_.assign(static_cast<std::size_t>(texW_ * texH_ * 4), 0);

  glCreateTextures(GL_TEXTURE_2D, 1, &tex_);
  glTextureStorage2D(tex_, 1, GL_RGBA8, texW_, texH_);
  glTextureParameteri(tex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTextureParameteri(tex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTextureSubImage2D(tex_, 0, 0, 0, texW_, texH_,
                      GL_RGBA, GL_UNSIGNED_BYTE, buf_.data());
}

void MinimapRenderer::setPixel(int px, int py, uint8_t r, uint8_t g, uint8_t b) {
  if (px < 0 || py < 0 || px >= texW_ || py >= texH_) return;
  const int off = (py * texW_ + px) * 4;
  buf_[static_cast<std::size_t>(off + 0)] = r;
  buf_[static_cast<std::size_t>(off + 1)] = g;
  buf_[static_cast<std::size_t>(off + 2)] = b;
  buf_[static_cast<std::size_t>(off + 3)] = 255;
}

void MinimapRenderer::fillRect(int px, int py, int pw, int ph,
                                uint8_t r, uint8_t g, uint8_t b) {
  for (int dy = 0; dy < ph; ++dy)
    for (int dx = 0; dx < pw; ++dx)
      setPixel(px + dx, py + dy, r, g, b);
}

void MinimapRenderer::rebuild(const shared::WorldMapFile& map,
                               const std::vector<shared::NpcSpawn>& npcs) {
  if (!tex_) return;

  // Resize texture if map dimensions changed.
  const int needW = map.width  * kPxPerTile;
  const int needH = map.height * kPxPerTile;
  if (needW != texW_ || needH != texH_) {
    init(map.width, map.height);
  }

  buf_.assign(static_cast<std::size_t>(texW_ * texH_ * 4), 0);

  // ---- Terrain tiles -------------------------------------------------------
  for (int ty = 0; ty < map.height; ++ty) {
    for (int tx = 0; tx < map.width; ++tx) {
      if (ty >= static_cast<int>(map.tiles.size())) continue;
      if (tx >= static_cast<int>(map.tiles[ty].size())) continue;
      const auto& tile = map.tiles[ty][tx];

      float fr = 0.24f, fg = 0.49f, fb = 0.20f;  // fallback green
      hexToRgbf(tile.groundColor.c_str(), fr, fg, fb);

      // Darken if obstacle present.
      if (tile.obstacle != shared::ObstacleType::none) {
        switch (tile.obstacle) {
          case shared::ObstacleType::tree:
            fr = 0.07f; fg = 0.22f; fb = 0.04f; break; // dark tree green
          case shared::ObstacleType::rock:
            fr = 0.40f; fg = 0.40f; fb = 0.40f; break; // grey
          case shared::ObstacleType::chest:
            fr = 0.55f; fg = 0.45f; fb = 0.10f; break; // gold
          case shared::ObstacleType::fence:
            fr = 0.36f; fg = 0.22f; fb = 0.08f; break; // wood brown
          default:
            fr *= kObstacleDarken;
            fg *= kObstacleDarken;
            fb *= kObstacleDarken;
            break;
        }
      }

      const auto r = static_cast<uint8_t>(std::clamp(fr, 0.0f, 1.0f) * 255.0f);
      const auto g = static_cast<uint8_t>(std::clamp(fg, 0.0f, 1.0f) * 255.0f);
      const auto b = static_cast<uint8_t>(std::clamp(fb, 0.0f, 1.0f) * 255.0f);

      // OSRS minimap is drawn bottom-up (tile 0,0 is top-left in map coords;
      // OpenGL textures origin bottom-left, but ImGui flips them back). We
      // store tile (tx, ty) at pixel row (height-1-ty) so Y=0 = top of image
      // when displayed via ImGui::Image with uv0=(0,0) uv1=(1,1).
      const int px = tx * kPxPerTile;
      const int py = ty * kPxPerTile;
      fillRect(px, py, kPxPerTile, kPxPerTile, r, g, b);
    }
  }

  // ---- NPC markers (2×2 pixel dot in the centre of the tile block) --------
  for (const auto& npc : npcs) {
    const int px = npc.tileX * kPxPerTile + kPxPerTile / 2 - 1;
    const int py = npc.tileY * kPxPerTile + kPxPerTile / 2 - 1;
    uint8_t nr = 255, ng = 255, nb = 0;  // yellow default
    if (npc.kind == "shopkeeper") { nr = 160; ng = 0; nb = 200; } // purple
    fillRect(px, py, 2, 2, nr, ng, nb);
  }

  // ---- Spawn point (white cross) ------------------------------------------
  const int sx = map.spawnPoint[0] * kPxPerTile + kPxPerTile / 2;
  const int sy = map.spawnPoint[1] * kPxPerTile + kPxPerTile / 2;
  for (int d = -2; d <= 2; ++d) {
    setPixel(sx + d, sy, 255, 255, 255);
    setPixel(sx, sy + d, 255, 255, 255);
  }

  glTextureSubImage2D(tex_, 0, 0, 0, texW_, texH_,
                      GL_RGBA, GL_UNSIGNED_BYTE, buf_.data());
}

}  // namespace editor
