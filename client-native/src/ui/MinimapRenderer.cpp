// MinimapRenderer.cpp
// See MinimapRenderer.hpp for the full design notes.

#include "ui/MinimapRenderer.hpp"

#include "world/OverlayMaterials.hpp"
#include "world/OverlayShapes.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui {

// ── Hex colour helper (local copy; avoids pulling in TerrainBuilder) ─────────
static void hexToRgb8(const std::string& hex,
                      uint8_t& r, uint8_t& g, uint8_t& b)
{
    const char* s = hex.c_str();
    if (*s == '#') ++s;
    unsigned int ri = 0x3d, gi = 0x7a, bi = 0x20; // fallback green
    if (std::strlen(s) >= 6) {
        auto h2 = [](const char* p) {
            unsigned int v = 0;
            for (int i = 0; i < 2; ++i) {
                char c = p[i];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            }
            return v;
        };
        ri = h2(s);
        gi = h2(s + 2);
        bi = h2(s + 4);
    }
    r = static_cast<uint8_t>(ri);
    g = static_cast<uint8_t>(gi);
    b = static_cast<uint8_t>(bi);
}

// ── Obstacle colour hints (matches editor MinimapRenderer) ────────────────────
static void obstacleColor(const std::string& obs,
                           uint8_t& r, uint8_t& g, uint8_t& b)
{
    if      (obs == "tree")         { r =  18; g =  56; b =  10; return; } // dark tree green
    else if (obs == "rock")         { r = 102; g = 102; b = 102; return; } // grey
    else if (obs == "chest")        { r = 140; g = 115; b =  26; return; } // gold
    else if (obs == "fence")        { r =  92; g =  56; b =  20; return; } // wood brown
    else if (obs == "fishing_spot") { r =  60; g = 130; b = 200; return; } // light blue
    // Generic darken
    r = static_cast<uint8_t>(r * 0.55f);
    g = static_cast<uint8_t>(g * 0.55f);
    b = static_cast<uint8_t>(b * 0.55f);
}

// ── Pixel helpers ─────────────────────────────────────────────────────────────
void MinimapRenderer::setPixel(int px, int py, uint8_t r, uint8_t g, uint8_t b)
{
    if (px < 0 || py < 0 || px >= baseTexW_ || py >= baseTexH_) return;
    const int off = (py * baseTexW_ + px) * 4;
    baseBuf_[static_cast<size_t>(off)]     = r;
    baseBuf_[static_cast<size_t>(off + 1)] = g;
    baseBuf_[static_cast<size_t>(off + 2)] = b;
    baseBuf_[static_cast<size_t>(off + 3)] = 255;
}

void MinimapRenderer::fillRect(int px, int py, int pw, int ph,
                                uint8_t r, uint8_t g, uint8_t b)
{
    for (int dy = 0; dy < ph; ++dy)
        for (int dx = 0; dx < pw; ++dx)
            setPixel(px + dx, py + dy, r, g, b);
}

// ── Init / destroy ────────────────────────────────────────────────────────────
void MinimapRenderer::init()
{
    // Composite FBO (156×156, RGBA8, no depth needed)
    glCreateFramebuffers(1, &compositeFbo_);
    glCreateTextures(GL_TEXTURE_2D, 1, &compositeTex_);
    glTextureStorage2D(compositeTex_, 1, GL_RGBA8, kSize, kSize);
    glTextureParameteri(compositeTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(compositeTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glNamedFramebufferTexture(compositeFbo_, GL_COLOR_ATTACHMENT0, compositeTex_, 0);

    // Empty VAO for fullscreen triangle (no buffers — gl_VertexID only)
    glCreateVertexArrays(1, &quadVao_);

    // Compile composite shader
    compositeShader_.fromFiles("shaders/minimap_composite.vert",
                                "shaders/minimap_composite.frag");
    if (!compositeShader_.isValid())
        std::fprintf(stderr, "[MinimapRenderer] failed to compile composite shader\n");
}

void MinimapRenderer::destroy()
{
    if (compositeFbo_) { glDeleteFramebuffers(1, &compositeFbo_); compositeFbo_ = 0; }
    if (compositeTex_) { glDeleteTextures(1, &compositeTex_);     compositeTex_ = 0; }
    if (baseTex_)      { glDeleteTextures(1, &baseTex_);          baseTex_      = 0; }
    if (quadVao_)      { glDeleteVertexArrays(1, &quadVao_);      quadVao_      = 0; }
    baseBuf_.clear();
    baseTexW_ = baseTexH_ = 0;
}

// ── buildBaseLayer ────────────────────────────────────────────────────────────
// Registers the map and invalidates the cached region; the raster itself runs
// lazily in updateFrame() via ensureRegion() once the player position is known.
void MinimapRenderer::buildBaseLayer(const shared::WorldMapFile& map)
{
    map_ = &map;
    regionCenterX_ = regionCenterY_ = INT_MIN;   // force re-raster
}

// ── ensureRegion ──────────────────────────────────────────────────────────────
// Rasterises the kRegionTiles² window of the map around (centerTx, centerTy)
// into the base texture. Skipped while the center stays within kRegionSlack
// tiles of the last build. Maps smaller than the region raster whole.
void MinimapRenderer::ensureRegion(int centerTx, int centerTy)
{
    if (!map_) return;
    const auto& map = *map_;
    if (regionCenterX_ != INT_MIN &&
        std::abs(centerTx - regionCenterX_) <= kRegionSlack &&
        std::abs(centerTy - regionCenterY_) <= kRegionSlack) return;

    regionCenterX_ = centerTx;
    regionCenterY_ = centerTy;
    regionW_ = std::min(kRegionTiles, map.width);
    regionH_ = std::min(kRegionTiles, map.height);
    regionOriginX_ = std::clamp(centerTx - regionW_ / 2, 0, map.width  - regionW_);
    regionOriginY_ = std::clamp(centerTy - regionH_ / 2, 0, map.height - regionH_);

    const int texW = regionW_ * kPxPerTile;
    const int texH = regionH_ * kPxPerTile;
    if (texW <= 0 || texH <= 0) return;

    // Reallocate base texture if size changed.
    if (texW != baseTexW_ || texH != baseTexH_) {
        if (baseTex_) glDeleteTextures(1, &baseTex_);
        glCreateTextures(GL_TEXTURE_2D, 1, &baseTex_);
        glTextureStorage2D(baseTex_, 1, GL_RGBA8, texW, texH);
        glTextureParameteri(baseTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(baseTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(baseTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(baseTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        baseTexW_ = texW;
        baseTexH_ = texH;
    }

    baseBuf_.assign(static_cast<size_t>(texW * texH * 4), 0);

    // ---- Terrain tiles (region window; void tiles stay black) ---------------
    for (int ry = 0; ry < regionH_; ++ry) {
        const int ty = regionOriginY_ + ry;
        if (ty >= static_cast<int>(map.tiles.size())) continue;
        for (int rx = 0; rx < regionW_; ++rx) {
            const int tx = regionOriginX_ + rx;
            if (tx >= static_cast<int>(map.tiles[ty].size())) continue;
            const auto& tile = map.tiles[ty][tx];
            if (tile.isVoid) continue;   // unassigned world cell → black

            uint8_t r, g, b;
            hexToRgb8(tile.groundColor, r, g, b);

            if (!tile.obstacle.empty() && tile.obstacle != "none") {
                obstacleColor(tile.obstacle, r, g, b);
            }

            fillRect(rx * kPxPerTile, ry * kPxPerTile, kPxPerTile, kPxPerTile, r, g, b);
        }
    }

    // ---- Overlay tiles (paths / floors / water) ------------------------------
    // Rasterise each overlay's shape per sub-pixel so triangular / partial
    // shapes read as triangles, filled with the material's representative colour.
    // (u,v) maps to the same tile-local space the world mesh uses (see
    // OverlayRenderer's 180° emit): u = 1 - eastFrac, v = northFrac.
    {
        const auto& mats = world::overlayMaterials();
        for (const auto& ov : map.overlayTiles) {
            if (ov.tileX < regionOriginX_ || ov.tileY < regionOriginY_ ||
                ov.tileX >= regionOriginX_ + regionW_ ||
                ov.tileY >= regionOriginY_ + regionH_) continue;
            if (ov.materialId <= 0 ||
                ov.materialId >= static_cast<int>(mats.size())) continue;
            const auto& m = mats[static_cast<size_t>(ov.materialId)];
            const int px0 = (ov.tileX - regionOriginX_) * kPxPerTile;
            const int py0 = (ov.tileY - regionOriginY_) * kPxPerTile;
            for (int sy = 0; sy < kPxPerTile; ++sy) {
                for (int sx = 0; sx < kPxPerTile; ++sx) {
                    const float u = 1.0f - (sx + 0.5f) / static_cast<float>(kPxPerTile);
                    const float v = 1.0f - (sy + 0.5f) / static_cast<float>(kPxPerTile);
                    if (world::shapeCoversUV(ov.shape, u, v, ov.rotation))
                        setPixel(px0 + sx, py0 + sy, m.mr, m.mg, m.mb);
                }
            }
        }
    }

    // ---- Walls + pillars (1px white edge/corner lines, OSRS-style) -----------
    // +tileX = right, +tileY = down in the base texture. Lines hug the inside
    // edge so building perimeters read as a continuous white outline.
    for (const auto& w : map.walls) {
        const int o = w.orient & 7;
        // Multi-tile walls draw their edge on every footprint tile.
        for (const auto& f : shared::wallFootprint(w)) {
            const int fx = f.first, fy = f.second;
            if (fx < regionOriginX_ || fy < regionOriginY_ ||
                fx >= regionOriginX_ + regionW_ ||
                fy >= regionOriginY_ + regionH_) continue;
            const int px = (fx - regionOriginX_) * kPxPerTile;
            const int py = (fy - regionOriginY_) * kPxPerTile;
            const int n  = kPxPerTile;
            constexpr uint8_t R = 255, G = 255, B = 255;
            if (w.pillar) {
                const int cx = (o == 0 || o == 2) ? px + n - 1 : px;   // +X corners on the right
                const int cy = (o == 0 || o == 6) ? py + n - 1 : py;   // +Z corners on the bottom
                setPixel(cx, cy, R, G, B);
            } else if ((o & 1) == 0) {
                if      (o == 0) for (int i = 0; i < n; ++i) setPixel(px + i,     py + n - 1, R, G, B); // +Z (bottom)
                else if (o == 2) for (int i = 0; i < n; ++i) setPixel(px + n - 1, py + i,     R, G, B); // +X (right)
                else if (o == 4) for (int i = 0; i < n; ++i) setPixel(px + i,     py,         R, G, B); // -Z (top)
                else             for (int i = 0; i < n; ++i) setPixel(px,         py + i,     R, G, B); // -X (left)
            } else {
                if (o == 1 || o == 5) for (int i = 0; i < n; ++i) setPixel(px + n - 1 - i, py + i, R, G, B); // "/"
                else                  for (int i = 0; i < n; ++i) setPixel(px + i,         py + i, R, G, B); // "\"
            }
        }
    }

    glTextureSubImage2D(baseTex_, 0, 0, 0, texW, texH,
                        GL_RGBA, GL_UNSIGNED_BYTE, baseBuf_.data());
}

// ── updateFrame ───────────────────────────────────────────────────────────────
void MinimapRenderer::updateFrame(
    float playerX,
    float playerY,
    const shared::PlayerState*                                  localPlayer,
    const std::unordered_map<std::string, shared::PlayerState>& allPlayers,
    const std::vector<shared::NPCState>&                        npcs,
    const std::vector<shared::DroppedItemState>&                items,
    float cameraYaw,
    float tileRadius,
    int destTileX,
    int destTileY)
{
    // Lazy region (re)build now that the player position is known.
    ensureRegion(static_cast<int>(playerX), static_cast<int>(playerY));
    if (!compositeFbo_ || !compositeShader_.isValid() || !baseTex_) return;

    // ---- Build indicator dots -----------------------------------------------
    // All positions are in circle space [-1,1].
    // Rotation: R(+yaw) maps tile offset → circle display position.
    //   cx = cos(yaw)*rotX - sin(yaw)*rotY
    //   cy = sin(yaw)*rotX + cos(yaw)*rotY
    // where rotX = tileOffX/tileRadius, rotY = tileOffY/tileRadius.
    //
    // Dot index 0 = local player (white, always at center (0,0)).

    struct Dot { float x, y, r, g, b; };
    static Dot dotBuf[128];
    int dotCount = 0;

    auto addDot = [&](float cx, float cy, float r, float g, float b) {
        if (dotCount >= 128) return;
        // Discard dots outside the visible disc (with a small tolerance for
        // the border ring that the shader draws at dist 0.93+).
        if (cx*cx + cy*cy > 1.0f) return;
        dotBuf[dotCount++] = { cx, cy, r, g, b };
    };

    // tileToCircle: maps a tile position to circle display space [-1,1].
    // Negates cx to compensate for lookAtLH screen convention (east = left).
    auto tileToCircle = [&](float tx, float ty, float& cx, float& cy) {
        float rotX = (tx - playerX) / tileRadius;
        float rotY = (ty - playerY) / tileRadius;
        float c = std::cos(cameraYaw), s = std::sin(cameraYaw);
        cx = -(c * rotX - s * rotY);   // negate X: east appears on left
        cy =   s * rotX + c * rotY;
    };

    // Local player — always at center, index 0.
    addDot(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

    if (localPlayer) {
        // Remote players — white.
        for (const auto& [id, rp] : allPlayers) {
            if (rp.dying) continue;
            float cx, cy;
            tileToCircle(static_cast<float>(rp.tileX), static_cast<float>(rp.tileY), cx, cy);
            addDot(cx, cy, 1.0f, 1.0f, 1.0f);
        }

        // NPCs — yellow.
        for (const auto& n : npcs) {
            if (n.dying) continue;
            float cx, cy;
            tileToCircle(static_cast<float>(n.tileX), static_cast<float>(n.tileY), cx, cy);
            addDot(cx, cy, 1.0f, 0.85f, 0.0f);
        }

        // Dropped items — red.
        for (const auto& di : items) {
            float cx, cy;
            tileToCircle(static_cast<float>(di.tileX), static_cast<float>(di.tileY), cx, cy);
            addDot(cx, cy, 1.0f, 0.25f, 0.25f);
        }
    }

    // ---- Composite pass -------------------------------------------------------
    // Save state
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, compositeFbo_);
    glViewport(0, 0, kSize, kSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    compositeShader_.use();

    // Bind base texture to unit 0.
    glBindTextureUnit(0, baseTex_);
    compositeShader_.setInt("uBaseTex", 0);

    // Player UV in base texture space — uses interpolated float position for smooth scroll.
    glm::vec2 playerUV{0.5f, 0.5f};  // fallback = center
    if (baseTexW_ > 0 && baseTexH_ > 0) {
        // Player position relative to the rasterised region window.
        const float rx = playerX - static_cast<float>(regionOriginX_);
        const float ry = playerY - static_cast<float>(regionOriginY_);
        playerUV.x = (rx * static_cast<float>(kPxPerTile) + static_cast<float>(kPxPerTile) * 0.5f)
                     / static_cast<float>(baseTexW_);
        playerUV.y = (ry * static_cast<float>(kPxPerTile) + static_cast<float>(kPxPerTile) * 0.5f)
                     / static_cast<float>(baseTexH_);
    }
    compositeShader_.setVec2("uPlayerUV", playerUV);
    compositeShader_.setFloat("uYaw", cameraYaw);

    // zoomUV: how many UV units one "tileRadius circle unit" covers, per axis.
    // One tile = kPxPerTile pixels = kPxPerTile/texDim UV units, so a circle
    // unit covers (kPxPerTile/texDim) * tileRadius. The axes differ when the
    // region texture is non-square (non-square multi-chunk worlds).
    glm::vec2 zoomUV{0.25f, 0.25f}; // fallback
    if (baseTexW_ > 0 && baseTexH_ > 0 && tileRadius > 0.0f) {
        zoomUV.x = (static_cast<float>(kPxPerTile) / static_cast<float>(baseTexW_)) * tileRadius;
        zoomUV.y = (static_cast<float>(kPxPerTile) / static_cast<float>(baseTexH_)) * tileRadius;
    }
    compositeShader_.setVec2("uZoomUV", zoomUV);

    compositeShader_.setInt("uDotCount", dotCount);

    // Upload dot arrays.
    // render::Shader doesn't have array uniform helpers, so use raw GL calls.
    GLuint prog = compositeShader_.id();
    for (int i = 0; i < dotCount; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "uDotPos[%d]", i);
        GLint loc = glGetUniformLocation(prog, name);
        if (loc >= 0) glUniform2f(loc, dotBuf[i].x, dotBuf[i].y);
        std::snprintf(name, sizeof(name), "uDotColor[%d]", i);
        loc = glGetUniformLocation(prog, name);
        if (loc >= 0) glUniform3f(loc, dotBuf[i].r, dotBuf[i].g, dotBuf[i].b);
    }

    // Destination triangle marker
    bool destActive = (destTileX >= 0 && destTileY >= 0 && localPlayer != nullptr);
    GLint uDestActiveLoc = glGetUniformLocation(prog, "uDestActive");
    GLint uDestPosLoc    = glGetUniformLocation(prog, "uDestPos");
    if (uDestActiveLoc >= 0) glUniform1i(uDestActiveLoc, destActive ? 1 : 0);
    if (destActive && uDestPosLoc >= 0) {
        float cx, cy;
        tileToCircle(static_cast<float>(destTileX), static_cast<float>(destTileY), cx, cy);
        glUniform2f(uDestPosLoc, cx, cy);
    }

    // Draw fullscreen triangle.
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Restore state.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);
    glEnable(GL_DEPTH_TEST);
}

} // namespace ui
