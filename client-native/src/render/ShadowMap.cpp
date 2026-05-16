#include "render/ShadowMap.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

namespace render {

ShadowMap::~ShadowMap() { destroy(); }

bool ShadowMap::init(int size) {
  destroy();
  size_ = size;

  glCreateTextures(GL_TEXTURE_2D, 1, &depthTex_);
  glTextureStorage2D(depthTex_, 1, GL_DEPTH_COMPONENT24, size, size);
  // Linear sampling enables hardware PCF on most drivers when used with
  // sampler2DShadow; we sample manually so plain GL_LINEAR + a flat
  // sampler2D is enough.
  glTextureParameteri(depthTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(depthTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(depthTex_, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_BORDER);
  glTextureParameteri(depthTex_, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_BORDER);
  // Border color = white (depth 1 = far) so samples outside the shadow
  // frustum are treated as fully lit, not perpetually shadowed.
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTextureParameterfv(depthTex_, GL_TEXTURE_BORDER_COLOR, white);

  glCreateFramebuffers(1, &fbo_);
  glNamedFramebufferTexture(fbo_, GL_DEPTH_ATTACHMENT, depthTex_, 0);
  // No color attachments — depth-only.
  glNamedFramebufferDrawBuffer(fbo_, GL_NONE);
  glNamedFramebufferReadBuffer(fbo_, GL_NONE);

  const GLenum status = glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "[ShadowMap] FBO incomplete: 0x%X\n", status);
    destroy();
    return false;
  }
  return true;
}

void ShadowMap::destroy() {
  if (fbo_)      glDeleteFramebuffers(1, &fbo_);
  if (depthTex_) glDeleteTextures(1, &depthTex_);
  fbo_ = depthTex_ = 0;
  size_ = 0;
}

void ShadowMap::beginPass() {
  glGetIntegerv(GL_VIEWPORT, prevVp_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, size_, size_);
  glClear(GL_DEPTH_BUFFER_BIT);
  // Front-face culling during the shadow pass cuts down on "peter panning"
  // (object-from-its-own-shadow artifacts) for closed convex casters.
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);
}

void ShadowMap::endPass() {
  glDisable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(prevVp_[0], prevVp_[1], prevVp_[2], prevVp_[3]);
}

glm::mat4 ShadowMap::lightViewProj(const glm::vec3& sunDir,
                                   const glm::vec3& mapCenter,
                                   float            halfExtent,
                                   float            depthRange) {
  // The sun "view" looks from sunPos toward mapCenter along sunDir.
  const glm::vec3 dir = glm::normalize(sunDir);
  // Place the eye well behind the map along the inverse light direction.
  const glm::vec3 eye = mapCenter - dir * (depthRange * 0.5f);
  // Up vector that isn't parallel with dir — world +Y normally, +Z if the
  // sun is straight down (avoids degenerate cross product).
  const glm::vec3 up = (std::abs(dir.y) > 0.99f)
                       ? glm::vec3(0.0f, 0.0f, 1.0f)
                       : glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::mat4 view = glm::lookAtLH(eye, mapCenter, up);
  const glm::mat4 proj = glm::orthoLH_ZO(-halfExtent, halfExtent,
                                         -halfExtent, halfExtent,
                                         0.0f, depthRange);
  return proj * view;
}

}  // namespace render
