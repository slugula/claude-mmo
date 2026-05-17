#pragma once

#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "world/WaterMesh.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>

namespace world {

// All real-time tuneable parameters for water appearance.
// Stored by the host (EditorApp / App) and passed to WaterRenderer::render().
struct WaterUniforms {
  // ---- Basic ----
  glm::vec3 shallowColor    = { 0.30f, 0.70f, 0.60f };
  glm::vec3 deepColor       = { 0.05f, 0.20f, 0.35f };
  float     waveSpeed       = 0.40f;
  float     waveHeight      = 0.08f;
  float     normalStrength  = 0.60f;
  float     reflectStrength = 0.50f;
  float     causticIntensity= 0.30f;
  float     foamThreshold   = 0.60f;
  // ---- Advanced ----
  float     waveScale       = 2.00f;
  float     causticScale    = 4.00f;
  float     causticSpeed    = 0.30f;
  glm::vec3 foamColor       = { 0.90f, 0.95f, 1.00f };
  float     foamSpeed       = 0.50f;
  float     foamScale       = 8.00f;
  float     parallaxDepth   = 0.04f;
  // waterOffset is in world units; also used by EditorApp banking.
  float     waterOffset     = 0.15f;
};

// Owns the water shader, normal-map GL texture, and WaterMesh.
// Usage:
//   init(normalMapPath)   — load shaders + texture once
//   rebuild(map, offset)  — call after map waterTiles change
//   render(...)           — call each frame after terrain is drawn
class WaterRenderer {
public:
  WaterRenderer()  = default;
  ~WaterRenderer() { destroy(); }

  WaterRenderer(const WaterRenderer&)            = delete;
  WaterRenderer& operator=(const WaterRenderer&) = delete;

  // Load water shaders from shaderDir (e.g. "shaders/water.vert").
  // Load normal map from normalMapPath; uses a flat fallback if missing.
  bool init(const std::string& shaderVertPath,
            const std::string& shaderFragPath,
            const std::string& normalMapPath);
  void destroy();

  void rebuild(const shared::WorldMapFile& map, float waterOffset);

  // Render water on top of the already-drawn scene.
  //   time          — elapsed seconds (for animation)
  //   viewProj      — combined view*projection matrix
  //   sceneColorTex — resolved FBO colour texture (for screen-space reflection)
  //   u             — appearance uniforms
  void render(float time,
              const glm::mat4& viewProj,
              GLuint           sceneColorTex,
              const WaterUniforms& u);

  bool valid() const { return shader_.isValid(); }

private:
  WaterMesh      mesh_;
  render::Shader shader_;
  GLuint         normalMapTex_ = 0;
};

}  // namespace world
