#pragma once

#include <glad/glad.h>

namespace render {

// Off-screen multisampled framebuffer for the main pass.
//
// Layout:
//   - fboMs:   multisampled FBO with renderbuffer color (RGBA8) + depth24
//              (the main scene pass renders here)
//   - fboResolve: single-sample FBO with a texture attachment
//                 (after the main pass, we blit fboMs -> fboResolve so the
//                 result is sampleable for post-processing or final UI draw)
//
// Created lazily-resizable: call resize() before binding when window size
// changes.
class MsaaFramebuffer {
public:
  MsaaFramebuffer() = default;
  MsaaFramebuffer(int width, int height, int samples = 4);
  ~MsaaFramebuffer();

  MsaaFramebuffer(const MsaaFramebuffer&)            = delete;
  MsaaFramebuffer& operator=(const MsaaFramebuffer&) = delete;

  // (Re)allocate the backing renderbuffers / texture. Safe to call repeatedly.
  void resize(int width, int height);

  // Bind the multisampled FBO as the draw target.
  void bind() const;

  // Blit the multisampled color attachment to the resolve FBO.
  // After this call, resolveColorTexture() is up-to-date.
  void resolve() const;

  // Blit the multisampled depth attachment to the resolve FBO depth texture.
  // After this call, resolveDepthTexture() is up-to-date.
  // Call this before rendering passes that need to sample scene depth (e.g. water foam).
  void resolveDepth() const;

  // Blit the resolved color to the default framebuffer (window).
  // Use this after resolve() to present the result.
  void blitToDefault(int targetWidth, int targetHeight) const;

  GLuint resolveColorTexture() const { return resolveColor_; }
  GLuint resolveDepthTexture() const { return resolveDepth_; }
  int width()  const { return width_;  }
  int height() const { return height_; }
  int samples() const { return samples_; }

private:
  void destroy();

  int width_   = 0;
  int height_  = 0;
  int samples_ = 0;

  GLuint fboMs_        = 0;
  GLuint colorRboMs_   = 0;
  GLuint depthRboMs_   = 0;

  GLuint fboResolve_   = 0;
  GLuint resolveColor_ = 0;
  GLuint resolveDepth_ = 0;
};

}  // namespace render
