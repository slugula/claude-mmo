#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace render {

// A simple directional shadow map: depth-only FBO with a single depth
// texture sampled via PCF in the receiving shaders. Sized N x N (square).
//
// Phase 6b scope: obstacles (trees, rocks) cast, terrain receives. Players
// + entities cast/receive in a later iteration.
class ShadowMap {
public:
  ShadowMap() = default;
  ~ShadowMap();

  ShadowMap(const ShadowMap&)            = delete;
  ShadowMap& operator=(const ShadowMap&) = delete;

  bool init(int size);
  void destroy();

  // Bind the FBO and prepare GL state for a depth-only pass. Records the
  // current viewport so endPass() can restore it.
  void beginPass();
  void endPass();

  GLuint depthTexture() const { return depthTex_; }
  GLuint fbo()          const { return fbo_;      }
  int    size()         const { return size_;     }

  // Orthographic light-space view-projection for a directional sun.
  // `mapCenter`  : world point the frustum centers on (X, Z = map middle)
  // `halfExtent` : half-width/height of the captured world area (e.g. 40)
  // `depthRange` : near-plane offset behind sunPos to far-plane in front
  static glm::mat4 lightViewProj(const glm::vec3& sunDir,
                                 const glm::vec3& mapCenter,
                                 float            halfExtent,
                                 float            depthRange = 80.0f);

private:
  GLuint fbo_       = 0;
  GLuint depthTex_  = 0;
  int    size_      = 0;
  // Saved viewport from beginPass(), restored on endPass().
  GLint  prevVp_[4] = {0, 0, 0, 0};
};

}  // namespace render
