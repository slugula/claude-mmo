#include "world/OverlayRenderer.hpp"

#include "world/OverlayMaterials.hpp"
#include "world/OverlayShapes.hpp"

#include <stb_image.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace world {

namespace {
struct OverlayVertex {
  glm::vec3 pos;
  glm::vec2 uv;
  float     materialId;
};

// Terrain-exact height at tile-local (u,v) given the 4 corner heights.
//   u: 0 = west, 1 = east   v: 0 = south, 1 = north
//
// IMPORTANT: this must match TerrainBuilder's triangulation, NOT a bilinear
// blend. Each terrain tile is split along the SW<->NE diagonal (the u == v
// line): tris are {SW,SE,NE} for u>=v and {SW,NE,NW} for u<v. Using the wrong
// (NW<->SE) diagonal makes the surface cross the terrain on sloped tiles, so
// the ground pokes through.
inline float terrainHeightAt(float u, float v,
                             float hSW, float hSE, float hNW, float hNE) {
  if (u >= v) return hSW + u * (hSE - hSW) + v * (hNE - hSE);  // SW,SE,NE
  return hSW + v * (hNW - hSW) + u * (hNE - hNW);              // SW,NE,NW
}
}  // namespace

// ---------------------------------------------------------------------------
bool OverlayRenderer::init(const std::string& vertPath,
                           const std::string& fragPath,
                           const std::string& texDirPrefix) {
  if (!shader_.fromFiles(vertPath, fragPath)) {
    std::fprintf(stderr, "[OverlayRenderer] shader load failed\n");
    return false;
  }

  const auto& mats = overlayMaterials();
  const int   layers = static_cast<int>(mats.size());
  const int   SZ = kOverlayTexSize;

  glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texArray_);
  glTextureStorage3D(texArray_, 1, GL_RGBA8, SZ, SZ, layers);
  glTextureParameteri(texArray_, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(texArray_, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTextureParameteri(texArray_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(texArray_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  for (int i = 0; i < layers; ++i) {
    const auto& m = mats[static_cast<std::size_t>(i)];
    if (m.texturePath.empty()) {
      // "none" sentinel — fill with flat magenta so misuse is visible.
      std::vector<unsigned char> magenta(static_cast<std::size_t>(SZ) * SZ * 4, 0);
      for (std::size_t p = 0; p < magenta.size(); p += 4) {
        magenta[p] = 255; magenta[p + 1] = 0; magenta[p + 2] = 255; magenta[p + 3] = 255;
      }
      glTextureSubImage3D(texArray_, 0, 0, 0, i, SZ, SZ, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, magenta.data());
      continue;
    }
    const std::string path =
        (std::filesystem::path(texDirPrefix) / m.texturePath).string();
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px || w != SZ || h != SZ) {
      std::fprintf(stderr,
                   "[OverlayRenderer] texture '%s' missing or not %dx%d (got %dx%d) — "
                   "layer %d left blank\n",
                   path.c_str(), SZ, SZ, w, h, i);
      if (px) stbi_image_free(px);
      continue;
    }
    glTextureSubImage3D(texArray_, 0, 0, 0, i, SZ, SZ, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
  }

  std::fprintf(stderr, "[OverlayRenderer] loaded %d material layers\n", layers);
  return true;
}

// ---------------------------------------------------------------------------
void OverlayRenderer::destroy() {
  if (ebo_)      { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
  if (vbo_)      { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
  if (vao_)      { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
  if (texArray_) { glDeleteTextures(1, &texArray_); texArray_ = 0; }
  indexCount_ = 0;
}

// ---------------------------------------------------------------------------
void OverlayRenderer::rebuild(const shared::WorldMapFile& map) {
  // Drop only the mesh buffers; keep the shader + texture array alive.
  if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
  if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
  if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
  indexCount_ = 0;

  const int W = map.width, H = map.height;
  const auto& vh = map.vertexHeights;
  const bool  vhValid = (static_cast<int>(vh.size()) == (W + 1) * (H + 1));

  auto cornerH = [&](int vc, int vr) -> float {
    if (!vhValid || vc < 0 || vc > W || vr < 0 || vr > H) return 0.f;
    return vh[static_cast<std::size_t>(vr * (W + 1) + vc)] * shared::kMaxTerrainH;
  };

  const auto& shapes = overlayShapeTriangles();
  const auto& mats   = overlayMaterials();

  std::vector<OverlayVertex> verts;
  std::vector<unsigned int>  indices;

  // Small lift to seat the overlay above the terrain; combined with the
  // glPolygonOffset at draw this keeps the surface from depth-fighting along
  // the tile's shared diagonal (visible as a view-dependent "hollow" centre).
  constexpr float kLift = 0.02f;

  for (const auto& ov : map.overlayTiles) {
    if (ov.materialId == shared::kWaterMaterialId) continue;  // water → WaterRenderer
    if (ov.materialId <= 0 ||
        ov.materialId >= static_cast<int>(mats.size())) continue;
    if (ov.shape < 0 || ov.shape >= kNumOverlayShapes) continue;
    const int tx = ov.tileX, ty = ov.tileY;
    if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;

    // Corner heights (mirror WaterMesh vertex indexing).
    const float hSW = cornerH(tx,     H - ty);
    const float hSE = cornerH(tx + 1, H - ty);
    const float hNW = cornerH(tx,     H - ty - 1);
    const float hNE = cornerH(tx + 1, H - ty - 1);

    const float uvScale = mats[static_cast<std::size_t>(ov.materialId)].uvScale;
    const float matF    = static_cast<float>(ov.materialId);

    auto emit = [&](float u, float v) {
      // Apply the tile's authored rotation first (about the tile centre)...
      rotateUV(u, v, ov.rotation);
      // ...then rotate the shape 180° about the tile centre so the rendered
      // orientation matches the editor's shape preview (whose +u is
      // screen-right, +v is screen-up). Without this, the camera's
      // east=screen-left / flipped-Z convention shows shapes mirrored.
      const float uu = 1.0f - u;
      const float vv = 1.0f - v;
      const float wx = static_cast<float>(tx) - 0.5f + uu;
      const float wz = static_cast<float>(ty) - 0.5f + vv;
      const float wy = terrainHeightAt(uu, vv, hSW, hSE, hNW, hNE) + kLift;
      OverlayVertex vert;
      vert.pos        = { wx, wy, wz };
      vert.uv         = { wx * uvScale, wz * uvScale };
      vert.materialId = matF;
      indices.push_back(static_cast<unsigned int>(verts.size()));
      verts.push_back(vert);
    };

    for (const auto& t : shapes[static_cast<std::size_t>(ov.shape)]) {
      emit(t.u0, t.v0);
      emit(t.u1, t.v1);
      emit(t.u2, t.v2);
    }
  }

  if (verts.empty()) return;

  glCreateVertexArrays(1, &vao_);
  glCreateBuffers(1, &vbo_);
  glCreateBuffers(1, &ebo_);

  glNamedBufferStorage(vbo_,
    static_cast<GLsizeiptr>(verts.size() * sizeof(OverlayVertex)),
    verts.data(), 0);
  glNamedBufferStorage(ebo_,
    static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
    indices.data(), 0);

  glVertexArrayVertexBuffer (vao_, 0, vbo_, 0, static_cast<GLsizei>(sizeof(OverlayVertex)));
  glVertexArrayElementBuffer(vao_, ebo_);

  glEnableVertexArrayAttrib (vao_, 0);
  glVertexArrayAttribFormat (vao_, 0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(OverlayVertex, pos)));
  glVertexArrayAttribBinding(vao_, 0, 0);

  glEnableVertexArrayAttrib (vao_, 1);
  glVertexArrayAttribFormat (vao_, 1, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(OverlayVertex, uv)));
  glVertexArrayAttribBinding(vao_, 1, 0);

  glEnableVertexArrayAttrib (vao_, 2);
  glVertexArrayAttribFormat (vao_, 2, 1, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(OverlayVertex, materialId)));
  glVertexArrayAttribBinding(vao_, 2, 0);

  indexCount_ = static_cast<int>(indices.size());
}

// ---------------------------------------------------------------------------
void OverlayRenderer::render(const OverlayLighting& L) {
  if (!vao_ || indexCount_ == 0 || !shader_.isValid()) return;

  shader_.use();
  shader_.setMat4 ("u_viewProj",       L.viewProj);
  shader_.setMat4 ("u_lightViewProj",  L.lightViewProj);
  shader_.setVec3 ("u_lightDir",       L.lightDir);
  shader_.setVec3 ("u_skyAmbientUp",   L.skyAmbientUp);
  shader_.setVec3 ("u_skyAmbientDown", L.skyAmbientDown);
  shader_.setVec3 ("u_sunColor",       L.sunColor);
  shader_.setVec3 ("u_paletteLevels",  L.paletteLevels);
  shader_.setFloat("u_paletteEnabled", L.paletteEnabled);
  shader_.setFloat("u_ambient",        L.ambient);
  shader_.setFloat("u_diffuse",        L.diffuse);
  shader_.setFloat("u_lightingEnabled",L.lightingEnabled);
  shader_.setInt  ("u_shadowMap",      L.shadowMapUnit);
  shader_.setFloat("u_shadowsEnabled", L.shadowsEnabled);
  shader_.setFloat("u_shadowDarkness", L.shadowDarkness);
  shader_.setFloat("u_shadowBias",     L.shadowBias);
  shader_.setFloat("u_shadowSoftness", L.shadowSoftness);
  shader_.setFloat("u_fogEnabled",     L.fogEnabled);
  shader_.setVec3 ("u_fogColor",       L.fogColor);
  shader_.setFloat("u_fogDensity",     L.fogDensity);
  shader_.setFloat("u_fogStart",       L.fogStart);

  glBindTextureUnit(2, texArray_);
  shader_.setInt("u_texArray", 2);

  // Overlays are flat decals on terrain — keep depth test, disable culling so
  // either winding shows, and rely on the small kLift to avoid z-fighting.
  const GLboolean cullWas    = glIsEnabled(GL_CULL_FACE);
  const GLboolean offsetWas  = glIsEnabled(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_CULL_FACE);
  // Pull the overlay toward the camera in depth so it consistently wins the
  // depth test against the terrain it's draped on — even where an overlay
  // triangle crosses the terrain's diagonal crease — while still being
  // occluded by taller geometry (trees, NPCs, walls) that sits well in front.
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.5f, -4.0f);
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  if (!offsetWas) glDisable(GL_POLYGON_OFFSET_FILL);
  if (cullWas) glEnable(GL_CULL_FACE);
}

}  // namespace world
