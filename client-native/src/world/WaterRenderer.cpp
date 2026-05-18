// STB_IMAGE_IMPLEMENTATION defined here so it is compiled exactly once
// across the whole game_core static library.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "world/WaterRenderer.hpp"

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

  return true;
}

// ---------------------------------------------------------------------------
void WaterRenderer::destroy() {
  mesh_.destroy();
  if (normalMapTex_) { glDeleteTextures(1, &normalMapTex_); normalMapTex_ = 0; }
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
  shader_.setInt("uNormalMap",  0);
  shader_.setInt("uSceneColor", 1);
  shader_.setInt("uSceneDepth", 2);
  glBindTextureUnit(0, normalMapTex_);
  glBindTextureUnit(1, sceneColorTex);
  glBindTextureUnit(2, sceneDepthTex);

  mesh_.draw();
}

}  // namespace world
