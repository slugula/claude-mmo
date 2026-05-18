// STB_IMAGE_IMPLEMENTATION defined here so it is compiled exactly once
// across the whole game_core static library.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "world/WaterRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace world {

// ---------------------------------------------------------------------------
static GLuint makeFlatNormalMap() {
  // 4×4 flat normal map: all normals pointing straight up (0,0,1) in tangent space
  // encoded as (0.5, 0.5, 1.0) -> (128, 128, 255) in RGB.
  constexpr int W = 4, H = 4;
  unsigned char data[W * H * 3];
  for (int i = 0; i < W * H; ++i) {
    data[i * 3 + 0] = 128;
    data[i * 3 + 1] = 128;
    data[i * 3 + 2] = 255;
  }
  GLuint tex = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &tex);
  glTextureStorage2D(tex, 1, GL_RGB8, W, H);
  glTextureSubImage2D(tex, 0, 0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, data);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return tex;
}

// ---------------------------------------------------------------------------
// Procedural fallback caustic texture — 64×64 single-channel sin-interference
// pattern, similar to the procedural shader formula but baked to a texture so
// the GPU code stays identical whether a real caustic image is loaded or not.
// ---------------------------------------------------------------------------
static GLuint makeFallbackCausticTex() {
  constexpr int SZ = 64;
  unsigned char data[SZ * SZ];
  for (int y = 0; y < SZ; ++y) {
    for (int x = 0; x < SZ; ++x) {
      const float fx = static_cast<float>(x) / SZ;
      const float fy = static_cast<float>(y) / SZ;
      const float c1 = std::abs(std::sin(fx * 3.14159f * 8.0f + fy * 3.14159f * 4.8f));
      const float c2 = std::abs(std::sin(fx * 3.14159f * 5.2f - fy * 3.14159f * 7.8f));
      const float v  = std::pow(c1 * c2, 1.8f);
      data[y * SZ + x] = static_cast<unsigned char>(std::min(v, 1.0f) * 255.0f);
    }
  }
  GLuint tex = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &tex);
  glTextureStorage2D(tex, 4, GL_R8, SZ, SZ);
  glTextureSubImage2D(tex, 0, 0, 0, SZ, SZ, GL_RED, GL_UNSIGNED_BYTE, data);
  glGenerateTextureMipmap(tex);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return tex;
}

// ---------------------------------------------------------------------------
bool WaterRenderer::init(const std::string& vertPath,
                         const std::string& fragPath,
                         const std::string& normalMapPath) {
  if (!shader_.fromFiles(vertPath, fragPath)) {
    std::fprintf(stderr, "[WaterRenderer] shader load failed (%s / %s)\n",
                 vertPath.c_str(), fragPath.c_str());
    return false;
  }

  // Try to load the normal map PNG
  int w = 0, h = 0, ch = 0;
  stbi_set_flip_vertically_on_load(0);
  unsigned char* pixels = stbi_load(normalMapPath.c_str(), &w, &h, &ch, 3);
  if (pixels && w > 0 && h > 0) {
    glCreateTextures(GL_TEXTURE_2D, 1, &normalMapTex_);
    glTextureStorage2D(normalMapTex_, 1, GL_RGB8, w, h);
    glTextureSubImage2D(normalMapTex_, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    // Mipmaps for better quality at distance
    glGenerateTextureMipmap(normalMapTex_);
    glTextureParameteri(normalMapTex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(normalMapTex_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(normalMapTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(normalMapTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(pixels);
    std::fprintf(stderr, "[WaterRenderer] loaded normal map %s (%dx%d)\n",
                 normalMapPath.c_str(), w, h);
  } else {
    std::fprintf(stderr, "[WaterRenderer] normal map not found, using flat fallback\n");
    normalMapTex_ = makeFlatNormalMap();
  }

  // Caustic texture starts as the procedural fallback; caller can upgrade it
  // later via loadCausticMap().
  causticTex_ = makeFallbackCausticTex();

  return true;
}

// ---------------------------------------------------------------------------
bool WaterRenderer::loadCausticMap(const std::string& path) {
  int w = 0, h = 0, ch = 0;
  stbi_set_flip_vertically_on_load(0);
  unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 1);
  if (!pixels || w <= 0 || h <= 0) {
    std::fprintf(stderr, "[WaterRenderer] caustic map load failed: %s\n", path.c_str());
    return false;
  }
  if (causticTex_) { glDeleteTextures(1, &causticTex_); causticTex_ = 0; }
  const int mips = 1 + static_cast<int>(std::log2(static_cast<float>(std::max(w, h))));
  glCreateTextures(GL_TEXTURE_2D, 1, &causticTex_);
  glTextureStorage2D(causticTex_, mips, GL_R8, w, h);
  glTextureSubImage2D(causticTex_, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, pixels);
  glGenerateTextureMipmap(causticTex_);
  glTextureParameteri(causticTex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(causticTex_, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTextureParameteri(causticTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTextureParameteri(causticTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  stbi_image_free(pixels);
  hasCausticMap_ = true;
  std::fprintf(stderr, "[WaterRenderer] loaded caustic map %s (%dx%d)\n",
               path.c_str(), w, h);
  return true;
}

// ---------------------------------------------------------------------------
void WaterRenderer::destroy() {
  mesh_.destroy();
  if (causticTex_)   { glDeleteTextures(1, &causticTex_);   causticTex_   = 0; }
  if (normalMapTex_) { glDeleteTextures(1, &normalMapTex_); normalMapTex_ = 0; }
  hasCausticMap_ = false;
}

// ---------------------------------------------------------------------------
void WaterRenderer::rebuild(const shared::WorldMapFile& map, float waterOffset) {
  mesh_.build(map, waterOffset);
}

// ---------------------------------------------------------------------------
void WaterRenderer::render(float time,
                            const glm::mat4& viewProj,
                            GLuint sceneColorTex,
                            GLuint sceneDepthTex,
                            const WaterUniforms& u) {
  if (!shader_.isValid() || mesh_.empty()) return;

  shader_.use();

  // Matrices
  shader_.setMat4("uViewProj", viewProj);

  // Time / animation
  shader_.setFloat("uTime",       time);
  shader_.setFloat("uWaveSpeed",  u.waveSpeed);
  shader_.setFloat("uWaveHeight", u.waveHeight);
  shader_.setFloat("uWaveScale",  u.waveScale);

  // Lighting / appearance
  shader_.setFloat("uNormalStrength",   u.normalStrength);
  shader_.setFloat("uReflectStrength",  u.reflectStrength);
  shader_.setFloat("uCausticIntensity", u.causticIntensity);
  shader_.setFloat("uCausticScale",     u.causticScale);
  shader_.setFloat("uCausticSpeed",     u.causticSpeed);
  shader_.setFloat("uFoamDepth",        u.foamDepth);
  shader_.setFloat("uFoamSpeed",        u.foamSpeed);
  shader_.setFloat("uFoamScale",        u.foamScale);
  shader_.setFloat("uParallaxDepth",    u.parallaxDepth);
  shader_.setVec3 ("uShallowColor",     u.shallowColor);
  shader_.setVec3 ("uDeepColor",        u.deepColor);
  shader_.setVec3 ("uFoamColor",        u.foamColor);

  // Textures
  shader_.setInt  ("uNormalMap",      0);
  shader_.setInt  ("uSceneColor",     1);
  shader_.setInt  ("uSceneDepth",     2);
  shader_.setInt  ("uCausticMap",     3);
  shader_.setFloat("uUseCausticMap",  hasCausticMap_ ? 1.0f : 0.0f);
  glBindTextureUnit(0, normalMapTex_);
  glBindTextureUnit(1, sceneColorTex);
  glBindTextureUnit(2, sceneDepthTex);
  glBindTextureUnit(3, causticTex_);

  mesh_.draw();
}

}  // namespace world
