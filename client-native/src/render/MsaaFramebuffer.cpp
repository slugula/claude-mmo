#include "render/MsaaFramebuffer.hpp"

#include <cstdio>

namespace render {

MsaaFramebuffer::MsaaFramebuffer(int width, int height, int samples)
    : samples_(samples) {
  resize(width, height);
}

MsaaFramebuffer::~MsaaFramebuffer() {
  destroy();
}

void MsaaFramebuffer::destroy() {
  if (resolveDepth_) glDeleteTextures(1, &resolveDepth_);
  if (resolveColor_) glDeleteTextures(1, &resolveColor_);
  if (fboResolve_)   glDeleteFramebuffers(1, &fboResolve_);
  if (depthRboMs_)   glDeleteRenderbuffers(1, &depthRboMs_);
  if (colorRboMs_)   glDeleteRenderbuffers(1, &colorRboMs_);
  if (fboMs_)        glDeleteFramebuffers(1, &fboMs_);
  fboMs_ = colorRboMs_ = depthRboMs_ = fboResolve_ = resolveColor_ = resolveDepth_ = 0;
}

void MsaaFramebuffer::resize(int width, int height) {
  if (width <= 0 || height <= 0) return;
  if (width == width_ && height == height_ && fboMs_ != 0) return;

  destroy();
  width_  = width;
  height_ = height;

  // ---- Multisampled FBO -----------------------------------------------------
  glGenFramebuffers(1, &fboMs_);
  glBindFramebuffer(GL_FRAMEBUFFER, fboMs_);

  glGenRenderbuffers(1, &colorRboMs_);
  glBindRenderbuffer(GL_RENDERBUFFER, colorRboMs_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRboMs_);

  glGenRenderbuffers(1, &depthRboMs_);
  glBindRenderbuffer(GL_RENDERBUFFER, depthRboMs_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH24_STENCIL8, width_, height_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRboMs_);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "[MsaaFramebuffer] multisampled FBO incomplete\n");
  }

  // ---- Resolve FBO ----------------------------------------------------------
  glGenFramebuffers(1, &fboResolve_);
  glBindFramebuffer(GL_FRAMEBUFFER, fboResolve_);

  glGenTextures(1, &resolveColor_);
  glBindTexture(GL_TEXTURE_2D, resolveColor_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColor_, 0);

  // Depth resolve texture — stores pre-water terrain depth for foam intersection.
  // Use NEAREST filtering since depth values should not be bilinearly interpolated.
  glGenTextures(1, &resolveDepth_);
  glBindTexture(GL_TEXTURE_2D, resolveDepth_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width_, height_, 0,
               GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, resolveDepth_, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "[MsaaFramebuffer] resolve FBO incomplete\n");
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFramebuffer::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fboMs_);
  glViewport(0, 0, width_, height_);
}

void MsaaFramebuffer::resolve() const {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fboMs_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboResolve_);
  glBlitFramebuffer(0, 0, width_, height_,
                    0, 0, width_, height_,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFramebuffer::resolveDepth() const {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fboMs_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboResolve_);
  glBlitFramebuffer(0, 0, width_, height_,
                    0, 0, width_, height_,
                    GL_DEPTH_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFramebuffer::blitToDefault(int targetWidth, int targetHeight) const {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fboResolve_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, width_, height_,
                    0, 0, targetWidth, targetHeight,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace render
