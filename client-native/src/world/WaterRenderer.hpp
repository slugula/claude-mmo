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
  glm::vec3 shallowColor       = { 0.30f, 0.70f, 0.60f };
  glm::vec3 deepColor          = { 0.05f, 0.20f, 0.35f };
  float     waveSpeed          = 0.40f;
  float     waveHeight         = 0.08f;
  float     normalStrength     = 0.60f;
  float     reflectStrength    = 0.80f;   // "water clarity" — how visible underwater content is
  float     causticIntensity   = 0.00f;
  float     foamWidth          = 0.50f;   // 0–1 shore-zone foam band width

  // ---- Depth & refraction (new) ----
  float     refractionStrength = 0.04f;   // UV distortion magnitude for underwater view
  float     depthFade          = 5.0f;    // how fast water color transitions shallow→deep
  float     shoreDepth         = 0.85f;   // 0–1 strength of shore-distance "fake depth" (flush water)
  float     foamContactWidth   = 0.3f;    // world-space depth threshold for contact foam
  float     nearPlane          = 0.1f;    // camera near plane (set per-frame by host)
  float     farPlane           = 500.0f;  // camera far plane

  // ---- Per-frame lighting (set by host) ----
  glm::vec3 cameraPos          = {};
  glm::vec3 sunDir             = { 0.f, -1.f, 0.f };
  float     specularStrength   = 0.70f;
  float     waterAlpha         = 0.82f;   // kept for legacy settings load compat

  // ---- Advanced ----
  float     waveScale          = 2.00f;
  float     causticScale       = 4.00f;
  float     causticSpeed       = 0.30f;
  glm::vec3 foamColor          = { 0.90f, 0.95f, 1.00f };
  float     foamSpeed          = 0.50f;
  float     foamScale          = 8.00f;
  float     parallaxDepth      = 0.04f;
  // waterOffset is in world units; also used by EditorApp banking.
  float     waterOffset        = 0.00f;

  // Relative path to the user-loaded caustic PNG ("" = procedural fallback).
  // Persisted in settings.cfg so the editor and client share the same caustic.
  std::string causticMapPath;
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

  // Load (or reload) a caustic texture from a PNG file.
  // Returns true on success. Falls back to the procedural caustic on failure.
  bool loadCausticMap(const std::string& path);

  void rebuild(const shared::WorldMapFile& map, float waterOffset);

  // Render water on top of the already-drawn scene.
  //   time          — elapsed seconds (for animation)
  //   viewProj      — combined view*projection matrix
  //   sceneColorTex — resolved FBO colour texture (for screen-space reflection)
  //   sceneDepthTex — resolved FBO depth texture (for depth-intersection foam)
  //   u             — appearance uniforms
  void render(float time,
              const glm::mat4& viewProj,
              GLuint           sceneColorTex,
              GLuint           sceneDepthTex,
              const WaterUniforms& u);

  bool valid() const { return shader_.isValid(); }

private:
  WaterMesh      mesh_;
  render::Shader shader_;
  GLuint         normalMapTex_  = 0;
  GLuint         causticTex_    = 0;
  bool           hasCausticMap_ = false;  // true once a file has been loaded
};

}  // namespace world
