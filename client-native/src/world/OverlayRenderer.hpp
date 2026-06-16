#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>

namespace world {

// Lighting / shadow / fog / palette parameters shared with the terrain pass.
// The host (App / EditorApp) fills this each frame from its own settings so the
// overlay surfaces match the terrain look exactly.
struct OverlayLighting {
  glm::mat4 viewProj      = glm::mat4(1.0f);
  glm::mat4 lightViewProj = glm::mat4(1.0f);
  glm::vec3 lightDir      = { 0.f, -1.f, 0.f };
  glm::vec3 skyAmbientUp   = { 0.16f, 0.34f, 0.62f };
  glm::vec3 skyAmbientDown = { 0.30f, 0.30f, 0.34f };
  glm::vec3 sunColor       = { 1.0f, 0.96f, 0.88f };
  glm::vec3 paletteLevels = { 8.f, 8.f, 8.f };
  glm::vec3 fogColor      = {};
  float     ambient         = 0.5f;
  float     diffuse         = 0.5f;
  float     lightingEnabled = 1.f;
  float     paletteEnabled  = 0.f;
  float     shadowsEnabled  = 0.f;
  float     shadowDarkness   = 0.5f;
  float     shadowBias       = 0.0015f;
  float     shadowSoftness   = 3.0f;   // PCSS max penumbra radius (texels)
  float     fogEnabled       = 0.f;
  float     fogDensity       = 0.015f;
  float     fogStart         = 0.f;
  int       shadowMapUnit    = 1;   // GL texture unit the shadow depth map is bound to
};

// Renders OSRS-style shaped overlay surfaces (paths, floors, etc.) on top of
// the terrain. Water overlays (materialId == shared::kWaterMaterialId) are
// skipped here — WaterRenderer draws those with animation.
//
// Usage:
//   init(vert, frag)       — load shader + build the material texture array
//   rebuild(map)           — call after overlayTiles change
//   render(lighting)       — call each frame after terrain, before water
class OverlayRenderer {
public:
  OverlayRenderer()  = default;
  ~OverlayRenderer() { destroy(); }

  OverlayRenderer(const OverlayRenderer&)            = delete;
  OverlayRenderer& operator=(const OverlayRenderer&) = delete;

  // Load shaders + build the GL_TEXTURE_2D_ARRAY from world::overlayMaterials().
  // texDirPrefix is prepended to each material's relative texturePath so the
  // host can resolve assets relative to the executable.
  bool init(const std::string& vertPath,
            const std::string& fragPath,
            const std::string& texDirPrefix);
  void destroy();

  // (Re)build the overlay mesh from map.overlayTiles, draped on terrain heights.
  void rebuild(const shared::WorldMapFile& map);

  // Draw the overlay surfaces. Binds the texture array to unit 2.
  void render(const OverlayLighting& L);

  bool valid()  const { return shader_.isValid(); }
  bool hasMesh() const { return indexCount_ > 0; }

private:
  render::Shader shader_;
  GLuint texArray_   = 0;
  GLuint vao_        = 0;
  GLuint vbo_        = 0;
  GLuint ebo_        = 0;
  int    indexCount_ = 0;
};

}  // namespace world
