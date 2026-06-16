#include "world/SkyRenderer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>   // implementation lives in WaterRenderer.cpp

#include <array>
#include <cstdio>

namespace world {

namespace {
// Unit cube, 36 vertices (position only). Winding is irrelevant — culling is
// disabled for the sky pass.
constexpr float kCube[] = {
  -1,-1,-1,  -1,-1, 1,  -1, 1, 1,  -1, 1, 1,  -1, 1,-1,  -1,-1,-1, // -X
   1,-1,-1,   1, 1,-1,   1, 1, 1,   1, 1, 1,   1,-1, 1,   1,-1,-1, // +X
  -1,-1,-1,   1,-1,-1,   1,-1, 1,   1,-1, 1,  -1,-1, 1,  -1,-1,-1, // -Y
  -1, 1,-1,  -1, 1, 1,   1, 1, 1,   1, 1, 1,   1, 1,-1,  -1, 1,-1, // +Y
  -1,-1,-1,  -1, 1,-1,   1, 1,-1,   1, 1,-1,   1,-1,-1,  -1,-1,-1, // -Z
  -1,-1, 1,   1,-1, 1,   1, 1, 1,   1, 1, 1,  -1, 1, 1,  -1,-1, 1, // +Z
};

// GL cubemap face order: +X,-X,+Y,-Y,+Z,-Z. Artist files are named by sign.
constexpr std::array<const char*, 6> kFaceFiles = {
  "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"
};
}  // namespace

SkyRenderer::~SkyRenderer() { destroy(); }

bool SkyRenderer::init(std::function<std::filesystem::path(const std::string&)> resolver) {
  resolver_ = std::move(resolver);
  if (!shader_.fromFiles(resolver_("shaders/skybox.vert"),
                         resolver_("shaders/skybox.frag"))) {
    std::fprintf(stderr, "[SkyRenderer] shader load failed\n");
    return false;
  }
  glCreateVertexArrays(1, &vao_);
  glCreateBuffers(1, &vbo_);
  glNamedBufferStorage(vbo_, sizeof(kCube), kCube, 0);
  glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, 3 * sizeof(float));
  glEnableVertexArrayAttrib(vao_, 0);
  glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(vao_, 0, 0);
  return true;
}

void SkyRenderer::destroy() {
  clearCubemap();
  if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
  if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

void SkyRenderer::clearCubemap() {
  if (cubemapTex_) { glDeleteTextures(1, &cubemapTex_); cubemapTex_ = 0; }
  cfg_.cubemap.clear();
}

bool SkyRenderer::loadCubemap(const std::string& name) {
  if (!resolver_ || name.empty()) { clearCubemap(); return false; }

  // Load all six faces first so a partial set doesn't clobber the current sky.
  struct Face { int w = 0, h = 0; unsigned char* px = nullptr; };
  std::array<Face, 6> faces;
  bool ok = true;
  stbi_set_flip_vertically_on_load(0);   // cubemap faces are not Y-flipped
  for (int i = 0; i < 6 && ok; ++i) {
    const auto rel = "assets/skybox/" + name + "/" + kFaceFiles[i];
    const auto path = resolver_(rel);
    int ch = 0;
    // Cubemaps live as loose files (not in assets.pak) — resolve to disk.
    faces[i].px = stbi_load(path.string().c_str(), &faces[i].w, &faces[i].h, &ch, 3);
    if (!faces[i].px) {
      std::fprintf(stderr, "[SkyRenderer] missing cubemap face %s\n", path.string().c_str());
      ok = false;
    }
  }

  if (ok) {
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &tex);
    const int s = faces[0].w;
    glTextureStorage2D(tex, 1, GL_RGB8, s, s);
    for (int i = 0; i < 6; ++i) {
      // glTextureSubImage3D layer i = cubemap face i (+X,-X,+Y,-Y,+Z,-Z).
      glTextureSubImage3D(tex, 0, 0, 0, i, faces[i].w, faces[i].h, 1,
                          GL_RGB, GL_UNSIGNED_BYTE, faces[i].px);
    }
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (cubemapTex_) glDeleteTextures(1, &cubemapTex_);
    cubemapTex_  = tex;
    cfg_.cubemap = name;
    std::fprintf(stdout, "[SkyRenderer] loaded cubemap '%s' (%dx%d/face)\n", name.c_str(), s, s);
  }

  for (auto& f : faces) if (f.px) stbi_image_free(f.px);
  return ok;
}

void SkyRenderer::render(const glm::mat4& viewProjNoTrans) {
  if (!vao_) return;

  // Sky sits at the far plane: keep depth test (LEQUAL so depth==1 passes) but
  // don't write depth, so the opaque scene already drawn this frame stays on
  // top and later geometry isn't occluded by the sky.
  GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_CULL_FACE);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);

  shader_.use();
  shader_.setMat4("u_viewProjNoTrans", viewProjNoTrans);
  shader_.setFloat("u_hasCubemap", cubemapTex_ ? 1.0f : 0.0f);
  shader_.setVec3 ("u_zenith",   cfg_.zenith);
  shader_.setVec3 ("u_horizon",  cfg_.horizon);
  shader_.setVec3 ("u_ground",   cfg_.ground);
  shader_.setFloat("u_exposure", cfg_.exposure);
  if (cubemapTex_) {
    glBindTextureUnit(0, cubemapTex_);
    shader_.setInt("u_sky", 0);
  }

  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  if (prevCull) glEnable(GL_CULL_FACE);
}

}  // namespace world
