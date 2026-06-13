#include "editor/MinimapRenderer.hpp"
#include "editor/EditorPalette.hpp"
#include "world/OverlayMaterials.hpp"
#include "world/OverlayShapes.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace editor {

// Pixels per tile — matches OSRS minimap resolution exactly.
static constexpr int kPxPerTile = 4;

// Darkness factor for obstacle overlay (0=black, 1=unchanged).
static constexpr float kObstacleDarken = 0.55f;

// Shared base-layer raster (see header). Mirrors the terrain/overlay/wall
// passes the minimap window uses, so World-view thumbnails match the minimap.
void rasterMapBase(const shared::WorldMapFile& map, int pxPerTile,
                   std::vector<uint8_t>& buf, int& outW, int& outH) {
  const int W = map.width, H = map.height;
  outW = W * pxPerTile;
  outH = H * pxPerTile;
  buf.assign(static_cast<std::size_t>(outW) * outH * 4, 0);

  auto setPx = [&](int px, int py, uint8_t r, uint8_t g, uint8_t b) {
    if (px < 0 || py < 0 || px >= outW || py >= outH) return;
    const std::size_t off = (static_cast<std::size_t>(py) * outW + px) * 4;
    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b; buf[off + 3] = 255;
  };
  auto fillRect = [&](int px, int py, int pw, int ph, uint8_t r, uint8_t g, uint8_t b) {
    for (int dy = 0; dy < ph; ++dy)
      for (int dx = 0; dx < pw; ++dx) setPx(px + dx, py + dy, r, g, b);
  };

  // Terrain tiles (+ obstacle tints).
  for (int ty = 0; ty < H && ty < static_cast<int>(map.tiles.size()); ++ty) {
    for (int tx = 0; tx < W && tx < static_cast<int>(map.tiles[ty].size()); ++tx) {
      const auto& tile = map.tiles[ty][tx];
      if (tile.isVoid) continue;   // unassigned world-cell filler → black
      float fr = 0.24f, fg = 0.49f, fb = 0.20f;
      hexToRgbf(tile.groundColor.c_str(), fr, fg, fb);
      if (!tile.obstacle.empty() && tile.obstacle != "none") {
        const auto& obs = tile.obstacle;
        if      (obs == "tree")  { fr = 0.07f; fg = 0.22f; fb = 0.04f; }
        else if (obs == "rock")  { fr = 0.40f; fg = 0.40f; fb = 0.40f; }
        else if (obs == "chest") { fr = 0.55f; fg = 0.45f; fb = 0.10f; }
        else if (obs == "fence") { fr = 0.36f; fg = 0.22f; fb = 0.08f; }
        else { fr *= kObstacleDarken; fg *= kObstacleDarken; fb *= kObstacleDarken; }
      }
      fillRect(tx * pxPerTile, ty * pxPerTile, pxPerTile, pxPerTile,
               static_cast<uint8_t>(std::clamp(fr, 0.0f, 1.0f) * 255.0f),
               static_cast<uint8_t>(std::clamp(fg, 0.0f, 1.0f) * 255.0f),
               static_cast<uint8_t>(std::clamp(fb, 0.0f, 1.0f) * 255.0f));
    }
  }

  // Overlays (paths / floors / water), shaped per sub-pixel.
  {
    const auto& mats = world::overlayMaterials();
    for (const auto& ov : map.overlayTiles) {
      if (ov.tileX < 0 || ov.tileY < 0 || ov.tileX >= W || ov.tileY >= H) continue;
      if (ov.materialId <= 0 || ov.materialId >= static_cast<int>(mats.size())) continue;
      const auto& m = mats[static_cast<std::size_t>(ov.materialId)];
      const int px0 = ov.tileX * pxPerTile, py0 = ov.tileY * pxPerTile;
      for (int sy = 0; sy < pxPerTile; ++sy)
        for (int sx = 0; sx < pxPerTile; ++sx) {
          const float u = 1.0f - (sx + 0.5f) / static_cast<float>(pxPerTile);
          const float v = 1.0f - (sy + 0.5f) / static_cast<float>(pxPerTile);
          if (world::shapeCoversUV(ov.shape, u, v, ov.rotation))
            setPx(px0 + sx, py0 + sy, m.mr, m.mg, m.mb);
        }
    }
  }

  // Walls + pillars (white edge/corner lines).
  for (const auto& w : map.walls) {
    if (w.tileX < 0 || w.tileY < 0 || w.tileX >= W || w.tileY >= H) continue;
    const int px = w.tileX * pxPerTile, py = w.tileY * pxPerTile, n = pxPerTile;
    constexpr uint8_t R = 255, G = 255, B = 255;
    const int o = w.orient & 7;
    if (w.pillar) {
      const int cx = (o == 0 || o == 2) ? px + n - 1 : px;
      const int cy = (o == 0 || o == 6) ? py + n - 1 : py;
      setPx(cx, cy, R, G, B);
    } else if ((o & 1) == 0) {
      if      (o == 0) for (int i = 0; i < n; ++i) setPx(px + i,     py + n - 1, R, G, B);
      else if (o == 2) for (int i = 0; i < n; ++i) setPx(px + n - 1, py + i,     R, G, B);
      else if (o == 4) for (int i = 0; i < n; ++i) setPx(px + i,     py,         R, G, B);
      else             for (int i = 0; i < n; ++i) setPx(px,         py + i,     R, G, B);
    } else {
      if (o == 1 || o == 5) for (int i = 0; i < n; ++i) setPx(px + n - 1 - i, py + i, R, G, B);
      else                  for (int i = 0; i < n; ++i) setPx(px + i,         py + i, R, G, B);
    }
  }
}

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

  // Shared base layer (terrain + overlays + walls), then add editor-only
  // NPC + spawn markers on top.
  int rw = 0, rh = 0;
  rasterMapBase(map, kPxPerTile, buf_, rw, rh);
  (void)rw; (void)rh;   // == texW_/texH_ (resize handled above)

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
