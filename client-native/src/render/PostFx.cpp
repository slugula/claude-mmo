#include "render/PostFx.hpp"

#include <algorithm>
#include <cstdio>

namespace render {

PostFx::~PostFx() { destroy(); }

bool PostFx::init(const std::function<std::filesystem::path(const std::string&)>& resolve) {
  const bool ok =
      down_.fromFiles(resolve("shaders/fullscreen.vert"), resolve("shaders/bloom_downsample.frag")) &&
      up_.fromFiles  (resolve("shaders/fullscreen.vert"), resolve("shaders/bloom_upsample.frag")) &&
      tone_.fromFiles(resolve("shaders/fullscreen.vert"), resolve("shaders/tonemap.frag"));
  if (!ok) {
    std::fprintf(stderr, "[PostFx] shader load failed\n");
    return false;
  }
  glCreateVertexArrays(1, &vao_);
  glCreateFramebuffers(1, &fbo_);
  return true;
}

void PostFx::destroy() {
  for (auto& m : mips_) if (m.tex) glDeleteTextures(1, &m.tex);
  mips_.clear();
  if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
  if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

void PostFx::resize(int width, int height) {
  if (width <= 0 || height <= 0) return;
  if (width == w_ && height == h_ && !mips_.empty()) return;
  for (auto& m : mips_) if (m.tex) glDeleteTextures(1, &m.tex);
  mips_.clear();
  w_ = width; h_ = height;

  // Mip[0] = half resolution, each subsequent halves again, down to kMaxMips
  // levels or 1px (whichever first).
  int mw = width, mh = height;
  for (int i = 0; i < kMaxMips; ++i) {
    mw = std::max(1, mw / 2);
    mh = std::max(1, mh / 2);
    Mip m; m.w = mw; m.h = mh;
    glCreateTextures(GL_TEXTURE_2D, 1, &m.tex);
    glTextureStorage2D(m.tex, 1, GL_RGBA16F, mw, mh);
    glTextureParameteri(m.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mips_.push_back(m);
    if (mw == 1 && mh == 1) break;
  }
}

void PostFx::render(GLuint hdrScene, int outW, int outH, const PostFxParams& p) {
  if (!valid() || mips_.empty()) return;

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glBindVertexArray(vao_);

  const int n = std::clamp(p.bloomMips, 1, static_cast<int>(mips_.size()));
  const bool doBloom = p.bloomEnabled && n >= 1;

  if (doBloom) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glDisable(GL_BLEND);

    // ── Downsample chain: hdrScene → mip0 → mip1 → … ───────────────────────
    down_.use();
    GLuint src = hdrScene;
    int srcW = w_, srcH = h_;
    for (int i = 0; i < n; ++i) {
      glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, mips_[i].tex, 0);
      glViewport(0, 0, mips_[i].w, mips_[i].h);
      down_.setInt  ("uSrc", 0);
      glBindTextureUnit(0, src);
      down_.setVec2 ("uTexel", glm::vec2(1.0f / srcW, 1.0f / srcH));
      down_.setInt  ("uBrightPass", i == 0 ? 1 : 0);
      down_.setFloat("uThreshold", p.bloomThreshold);
      down_.setFloat("uKnee", std::max(p.bloomKnee, 1e-3f));
      glDrawArrays(GL_TRIANGLES, 0, 3);
      src = mips_[i].tex; srcW = mips_[i].w; srcH = mips_[i].h;
    }

    // ── Upsample chain: mip[n-1] → … → mip0, additive ──────────────────────
    up_.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);
    for (int i = n - 1; i > 0; --i) {
      glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, mips_[i - 1].tex, 0);
      glViewport(0, 0, mips_[i - 1].w, mips_[i - 1].h);
      up_.setInt  ("uSrc", 0);
      glBindTextureUnit(0, mips_[i].tex);
      up_.setVec2 ("uTexel", glm::vec2(1.0f / mips_[i].w, 1.0f / mips_[i].h));
      up_.setFloat("uRadius", p.bloomRadius);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
  }

  // ── Tonemap + composite to the default framebuffer ──────────────────────
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, outW, outH);
  tone_.use();
  tone_.setInt  ("uScene", 0);
  glBindTextureUnit(0, hdrScene);
  tone_.setInt  ("uBloom", 1);
  glBindTextureUnit(1, mips_[0].tex);
  tone_.setFloat("uExposure", p.exposure);
  tone_.setFloat("uBloomIntensity", p.bloomIntensity);
  tone_.setInt  ("uBloomEnabled", doBloom ? 1 : 0);
  tone_.setInt  ("uTonemap", p.tonemap);
  tone_.setFloat("uGamma", p.gamma);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

}  // namespace render
