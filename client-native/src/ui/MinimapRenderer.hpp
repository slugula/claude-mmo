#pragma once
// MinimapRenderer — OSRS-style circular minimap rendered to an off-screen FBO.
//
// Rendering pipeline:
//   1. buildBaseLayer() (once)  — CPU-uploads 4px/tile RGBA8 base texture from
//      map groundColor / water tiles / obstacle color hints.
//   2. updateFrame() (per frame) — GL composite pass: rotates base texture around
//      the player UV by -cameraYaw, overlays entity dots, circle-masks to disc,
//      draws a gold border ring.  Output in compositeTex_.
//   3. texture()  — caller passes the result to Clay as a CLAY_IMAGE element.
//
// Circle-space convention (for click-to-walk):
//   normX = (pixelX - circleCenterX) / (kSize/2)  in [-1, 1]
//   normY = (pixelY - circleCenterY) / (kSize/2)  in [-1, 1]  (screen-y = down)
//
// Tile offset from click:
//   tileOffX = (cos(yaw)*normX + sin(yaw)*normY) * tileRadius
//   tileOffY = (-sin(yaw)*normX + cos(yaw)*normY) * tileRadius   (NOTE: -sin)

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

class MinimapRenderer {
public:
    // Display diameter in pixels.
    static constexpr int kSize       = 156;
    // Base texture resolution: 4 pixels per tile (matches editor MinimapRenderer).
    static constexpr int kPxPerTile  = 4;

    MinimapRenderer() = default;
    ~MinimapRenderer() { destroy(); }

    MinimapRenderer(const MinimapRenderer&)            = delete;
    MinimapRenderer& operator=(const MinimapRenderer&) = delete;

    // Must be called once after the GL context is valid and shaders are on disk.
    void init();
    void destroy();

    // Rebuild the static base layer from map data.  Call after map load/reload.
    void buildBaseLayer(const shared::WorldMapFile& map);

    // Update the composite texture each frame.
    // playerX/Y: sub-tick interpolated tile-space position (may be fractional).
    // localPlayer: used for dying-flag and other metadata; may be nullptr.
    // destTileX/Y: active destination tile (shows red triangle); pass -1 to hide.
    void updateFrame(
        float playerX,
        float playerY,
        const shared::PlayerState*                                    localPlayer,
        const std::unordered_map<std::string, shared::PlayerState>&   allPlayers,
        const std::vector<shared::NPCState>&                          npcs,
        const std::vector<shared::DroppedItemState>&                  items,
        float cameraYaw,
        float tileRadius,
        int destTileX = -1,
        int destTileY = -1);

    // Returns the 156×156 RGBA8 disc texture for use with Clay IMAGE.
    GLuint texture() const { return compositeTex_; }

    // True once init() + buildBaseLayer() have succeeded.
    bool isReady() const { return compositeFbo_ != 0 && compositeShader_.isValid(); }

private:
    // ---- Base texture (static, one tile = 4×4 px) ---------------------------
    GLuint               baseTex_  = 0;
    int                  baseTexW_ = 0;
    int                  baseTexH_ = 0;
    std::vector<uint8_t> baseBuf_;

    // ---- Composite FBO (156×156, rebuilt every frame) -----------------------
    GLuint               compositeFbo_ = 0;
    GLuint               compositeTex_ = 0;

    // ---- Fullscreen triangle (empty VAO, 3 vertices via gl_VertexID) --------
    GLuint               quadVao_ = 0;

    render::Shader       compositeShader_;

    // ---- Pixel helpers for base layer construction --------------------------
    void setPixel(int px, int py, uint8_t r, uint8_t g, uint8_t b);
    void fillRect(int px, int py, int pw, int ph, uint8_t r, uint8_t g, uint8_t b);
};

} // namespace ui
