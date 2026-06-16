#pragma once

#include "render/Shader.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace world {

// Data-driven sky description. Persisted in settings; the procedural gradient
// colours are used when no cubemap is loaded so the sky always renders.
struct SkyConfig {
  // Empty = procedural gradient. Otherwise the name of a folder under
  // assets/skybox/<name>/ containing px,nx,py,ny,pz,nz PNG faces.
  std::string cubemap;
  glm::vec3   zenith   = {0.16f, 0.34f, 0.62f};   // straight up
  glm::vec3   horizon  = {0.62f, 0.74f, 0.86f};   // horizon band
  glm::vec3   ground   = {0.30f, 0.30f, 0.34f};   // below the horizon
  float       exposure = 1.0f;
  glm::vec3   sunColor = {1.0f, 0.96f, 0.88f};     // directional light tint

  // --- Reserved for the future astrology skill (NOT rendered yet) ---
  // A separate star/constellation layer will composite over this base sky and
  // become click-selectable. Kept here so the sky stays the single source of
  // truth for "what the sky looks like".
  std::string starLayer;   // unused for now
};

// =====================================================================
// SkyRenderer — draws a background sky (cubemap or procedural gradient).
// =====================================================================
//
// Rendered once per frame right after the framebuffer clear, into the same
// (MSAA) target as the scene, at the far plane (depth 1.0, depth-write off) so
// the opaque scene draws over it. Owns one cube VAO + an optional cubemap.
class SkyRenderer {
public:
  SkyRenderer() = default;
  ~SkyRenderer();
  SkyRenderer(const SkyRenderer&)            = delete;
  SkyRenderer& operator=(const SkyRenderer&) = delete;

  // resolver maps a relative shader path -> absolute (host knows the exe dir).
  bool init(std::function<std::filesystem::path(const std::string&)> resolver);
  void destroy();

  // Load a 6-face cubemap from assets/skybox/<name>/{px,nx,py,ny,pz,nz}.png.
  // Returns false (and falls back to procedural) if any face is missing.
  bool loadCubemap(const std::string& name);
  void clearCubemap();   // revert to the procedural gradient

  const SkyConfig& config() const { return cfg_; }
  SkyConfig&       config()       { return cfg_; }
  bool             hasCubemap() const { return cubemapTex_ != 0; }

  // Hemispheric ambient source colours for sky-driven lighting (Phase 4). With
  // a cubemap loaded these are the averaged up/down faces so ambient matches the
  // imported sky; otherwise they fall back to the procedural gradient colours.
  glm::vec3 ambientSky()    const { return cubemapTex_ ? ambientSky_    : cfg_.zenith; }
  glm::vec3 ambientGround() const { return cubemapTex_ ? ambientGround_ : cfg_.ground; }

  // Draw the sky. viewProjNoTrans = projection * translation-stripped view.
  void render(const glm::mat4& viewProjNoTrans);

private:
  std::function<std::filesystem::path(const std::string&)> resolver_;
  render::Shader shader_;
  GLuint vao_        = 0;
  GLuint vbo_        = 0;
  GLuint cubemapTex_ = 0;   // 0 = procedural gradient
  SkyConfig cfg_;
  glm::vec3 ambientSky_    = {0.16f, 0.34f, 0.62f};   // avg of +Y face (set on load)
  glm::vec3 ambientGround_ = {0.30f, 0.30f, 0.34f};   // avg of -Y face (set on load)
};

}  // namespace world
