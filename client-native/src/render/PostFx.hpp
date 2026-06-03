#pragma once

#include "render/Shader.hpp"

#include <glad/glad.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace render {

// Tunable post-processing parameters (exposed in the debug menu + persisted).
struct PostFxParams {
  // Tonemap / exposure
  float exposure = 1.0f;     // linear multiplier before tonemap
  int   tonemap  = 2;        // 0 = none, 1 = Reinhard, 2 = ACES
  float gamma    = 2.2f;     // output gamma (1.0 = none)
  // Bloom
  bool  bloomEnabled   = true;
  float bloomThreshold = 1.0f;
  float bloomKnee      = 0.5f;
  float bloomIntensity = 0.5f;
  float bloomRadius    = 1.0f;
  int   bloomMips      = 6;   // blur width (more = wider, softer)
};

// HDR post pipeline: bloom (mip-chain downsample/upsample) + tonemap. Reads the
// resolved HDR scene texture and writes the final LDR image to the default
// framebuffer. UI is drawn afterwards so it is never tonemapped.
class PostFx {
public:
  PostFx() = default;
  ~PostFx();

  PostFx(const PostFx&)            = delete;
  PostFx& operator=(const PostFx&) = delete;

  // resolver maps a relative shader path → absolute path.
  bool init(const std::function<std::filesystem::path(const std::string&)>& resolve);
  void destroy();

  // (Re)allocate the bloom mip chain to the given size. Safe to call repeatedly.
  void resize(int width, int height);

  // Run bloom + tonemap on `hdrScene` and present to the default framebuffer at
  // (outW, outH). Restores its own GL state (blend off, default framebuffer).
  void render(GLuint hdrScene, int outW, int outH, const PostFxParams& p);

  bool valid() const { return tone_.isValid(); }

private:
  struct Mip { GLuint tex = 0; int w = 0, h = 0; };

  static constexpr int kMaxMips = 7;

  GLuint           fbo_ = 0;     // reused; target texture attached per pass
  GLuint           vao_ = 0;     // empty VAO for the fullscreen triangle
  std::vector<Mip> mips_;        // [0] = half-res … decreasing
  int              w_ = 0, h_ = 0;

  Shader down_, up_, tone_;
};

}  // namespace render
