#include "editor/EditorApp.hpp"

#include "app/WaterSettings.hpp"
#include "editor/EditorPalette.hpp"
#include "input/Picker.hpp"
#include "render/GlDebug.hpp"
#include "shared/SharedTypesJson.hpp"
#include "world/GltfLoader.hpp"
#include "world/TerrainBuilder.hpp"
#include "world/OverlayMaterials.hpp"
#include "world/OverlayShapes.hpp"
#include "world/SkeletonConfig.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>   // DockBuilder

#include <glm/gtc/matrix_transform.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace editor {

namespace {

// Water is stored in the overlay layer (materialId == water).
inline bool overlayIsWater(const shared::WorldMapFile& map, int x, int y) {
  for (const auto& o : map.overlayTiles)
    if (o.materialId == shared::kWaterMaterialId && o.tileX == x && o.tileY == y)
      return true;
  return false;
}
inline bool mapHasWater(const shared::WorldMapFile& map) {
  for (const auto& o : map.overlayTiles)
    if (o.materialId == shared::kWaterMaterialId) return true;
  return false;
}

constexpr int   kInitialWidth  = 1440;
constexpr int   kInitialHeight = 900;
constexpr int   kMsaaSamples   = 4;
constexpr const char* kTitle             = "Project L Editor";
constexpr const char* kTerrainVertPath   = "shaders/terrain.vert";
constexpr const char* kTerrainFragPath   = "shaders/terrain.frag";
constexpr const char* kWireframeVertPath = "shaders/wireframe.vert";
constexpr const char* kWireframeFragPath = "shaders/wireframe.frag";
constexpr const char* kObstacleVertPath  = "shaders/obstacle.vert";
constexpr const char* kObstacleFragPath  = "shaders/obstacle.frag";
constexpr const char* kSkinnedVertPath   = "shaders/skinned.vert";
constexpr const char* kSkinnedFragPath   = "shaders/skinned.frag";
constexpr const char* kShadowInstVertPath= "shaders/shadow_instanced.vert";
constexpr const char* kShadowFragPath    = "shaders/shadow.frag";
constexpr const char* kWaterVertPath     = "shaders/water.vert";
constexpr const char* kWaterFragPath     = "shaders/water.frag";
constexpr const char* kWaterNormalPath   = "assets/water_normal.png";
constexpr const char* kTreeModelPath     = "assets/models/tree.gltf";
constexpr int         kShadowMapSize     = 2048;

std::filesystem::path resolveFromExe(const char* rel) {
  wchar_t buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n == MAX_PATH) return std::filesystem::path(rel);
  return std::filesystem::path(buf).parent_path() / rel;
}

glm::vec3 sunDirectionFromYawPitch(float yawDeg, float pitchDeg) {
  const float yaw   = glm::radians(yawDeg);
  const float pitch = glm::radians(pitchDeg);
  const float c = std::cos(pitch);
  return { std::sin(yaw) * c, -std::sin(pitch), std::cos(yaw) * c };
}

constexpr const char* kDefaultGroundColor = "#4a7c2a";

// DockSpace window and node IDs (stable across frames)
constexpr const char* kDockSpaceWindowName = "##MainDockSpaceWindow";
constexpr const char* kDockSpaceName       = "MainDockSpace";
constexpr const char* kViewport3dName      = "3D Viewport##3dvp";
constexpr const char* kGridName            = "2D Grid##grid";
constexpr const char* kToolbarName         = "Tools##toolbar";
constexpr const char* kPropsName           = "Properties##props";
constexpr const char* kMinimapName         = "Minimap##mm";

} // namespace

// -----------------------------------------------------------------------
EditorApp::~EditorApp() {
  if (imguiInited_) shutdownImGui();
  destroyHoverMesh();
  if (blockedVao_) { glDeleteVertexArrays(1, &blockedVao_); blockedVao_ = 0; }
  if (blockedVbo_) { glDeleteBuffers(1, &blockedVbo_);      blockedVbo_ = 0; }
  dbDestroyPreviewFbo();
}

void EditorApp::dbInitPreviewFbo() {
  // 256×256 colour texture
  glCreateTextures(GL_TEXTURE_2D, 1, &dbPreviewTex_);
  glTextureStorage2D(dbPreviewTex_, 1, GL_RGBA8, 256, 256);
  glTextureParameteri(dbPreviewTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(dbPreviewTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // Depth renderbuffer
  glCreateRenderbuffers(1, &dbPreviewRbo_);
  glNamedRenderbufferStorage(dbPreviewRbo_, GL_DEPTH_COMPONENT24, 256, 256);
  // FBO
  glCreateFramebuffers(1, &dbPreviewFbo_);
  glNamedFramebufferTexture(dbPreviewFbo_, GL_COLOR_ATTACHMENT0, dbPreviewTex_, 0);
  glNamedFramebufferRenderbuffer(dbPreviewFbo_, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, dbPreviewRbo_);
  // Preview shader
  dbPreviewShader_.fromFiles(resolveFromExe("shaders/preview.vert"),
                             resolveFromExe("shaders/preview.frag"));
}

void EditorApp::dbDestroyPreviewFbo() {
  // Destroy primitive GPU resources
  for (auto& p : dbPreviewPrims_) {
    if (p.vao)    glDeleteVertexArrays(1, &p.vao);
    if (p.vboPos) glDeleteBuffers(1, &p.vboPos);
    if (p.vboNorm)glDeleteBuffers(1, &p.vboNorm);
    if (p.vboCol) glDeleteBuffers(1, &p.vboCol);
    if (p.ebo)    glDeleteBuffers(1, &p.ebo);
  }
  dbPreviewPrims_.clear();
  if (dbPreviewFbo_) { glDeleteFramebuffers(1, &dbPreviewFbo_);   dbPreviewFbo_ = 0; }
  if (dbPreviewTex_) { glDeleteTextures(1, &dbPreviewTex_);       dbPreviewTex_ = 0; }
  if (dbPreviewRbo_) { glDeleteRenderbuffers(1, &dbPreviewRbo_);  dbPreviewRbo_ = 0; }
}

void EditorApp::dbLoadPreviewModel(const std::string& modelPath, bool forceReload) {
  if (!forceReload && modelPath == dbPreviewLoadedPath_) return;

  // Release old primitive GPU resources
  for (auto& p : dbPreviewPrims_) {
    if (p.vao)    glDeleteVertexArrays(1, &p.vao);
    if (p.vboPos) glDeleteBuffers(1, &p.vboPos);
    if (p.vboNorm)glDeleteBuffers(1, &p.vboNorm);
    if (p.vboCol) glDeleteBuffers(1, &p.vboCol);
    if (p.ebo)    glDeleteBuffers(1, &p.ebo);
  }
  dbPreviewPrims_.clear();
  dbPreviewHasAnim_  = false;
  dbPreviewClips_.clear();
  dbPreviewLoadedPath_ = modelPath;
  dbPreviewCenter_     = glm::vec3(0.f);
  dbPreviewRadius_     = 1.0f;

  if (modelPath.empty()) return;

  // ---- Try SkinnedMesh first (handles animated glTF models) ---------------
  {
    std::string resolvedPath;
    for (const char* prefix : { "", "assets/" }) {
      auto candidate = resolveFromExe((std::string(prefix) + modelPath).c_str());
      if (std::filesystem::exists(candidate)) { resolvedPath = candidate.string(); break; }
    }
    if (!resolvedPath.empty() && dbPreviewSkinned_.load(resolvedPath)) {
      if (dbPreviewSkinned_.animationCount() > 0) {
        dbPreviewHasAnim_ = true;
        for (int i = 0; i < dbPreviewSkinned_.animationCount(); ++i) {
          const std::string* n = dbPreviewSkinned_.animationNameAt(i);
          dbPreviewClips_.push_back(n ? *n : "clip_" + std::to_string(i));
        }
        dbPreviewSkinned_.setClip("");  // default: first clip
        // Estimate radius from bounding box
        dbPreviewRadius_ = 1.5f;  // conservative; could compute AABB from joints
        return;  // animated preview ready — skip static path
      }
    }
  }

  // ---- Fall back to static primitive preview --------------------------------
  // Try the path as-is (relative to exe), then with assets/ prefix
  std::optional<world::GltfModel> model;
  for (const char* prefix : { "", "assets/" }) {
    auto p = resolveFromExe((std::string(prefix) + modelPath).c_str());
    model = world::loadGlb(p);
    if (model) break;
  }
  if (!model || model->primitives.empty()) return;

  // Compute model-space AABB from all primitives
  glm::vec3 bmin( 1e9f), bmax(-1e9f);
  for (const auto& prim : model->primitives) {
    for (size_t i = 0; i + 2 < prim.positions.size(); i += 3) {
      glm::vec3 v(prim.positions[i], prim.positions[i+1], prim.positions[i+2]);
      bmin = glm::min(bmin, v);
      bmax = glm::max(bmax, v);
    }
  }
  if (bmin.x <= bmax.x) {
    dbPreviewCenter_ = (bmin + bmax) * 0.5f;
    dbPreviewRadius_ = glm::length(bmax - bmin) * 0.5f + 0.01f;
  }

  // Upload each primitive to the GPU
  for (const auto& prim : model->primitives) {
    if (prim.positions.empty() || prim.indices.empty()) continue;
    DbPreviewPrim gp;
    gp.indexCount = static_cast<GLsizei>(prim.indices.size());
    if (prim.materialIndex >= 0 &&
        prim.materialIndex < (int)model->materials.size())
      gp.color = model->materials[prim.materialIndex].baseColor;

    glCreateBuffers(1, &gp.vboPos);
    glNamedBufferStorage(gp.vboPos,
      prim.positions.size() * sizeof(float), prim.positions.data(), 0);

    // Use flat normals if none supplied (some static props omit normals)
    std::vector<float> norms = prim.normals;
    if (norms.size() < prim.positions.size()) norms.assign(prim.positions.size(), 0.f);
    glCreateBuffers(1, &gp.vboNorm);
    glNamedBufferStorage(gp.vboNorm, norms.size() * sizeof(float), norms.data(), 0);

    // Per-vertex RGBA colours (white when the model has none).
    const std::size_t vcount = prim.positions.size() / 3;
    std::vector<float> cols = prim.colors;
    if (cols.size() != vcount * 4) cols.assign(vcount * 4, 1.0f);
    glCreateBuffers(1, &gp.vboCol);
    glNamedBufferStorage(gp.vboCol, cols.size() * sizeof(float), cols.data(), 0);

    glCreateBuffers(1, &gp.ebo);
    glNamedBufferStorage(gp.ebo,
      prim.indices.size() * sizeof(uint32_t), prim.indices.data(), 0);

    glCreateVertexArrays(1, &gp.vao);
    // position — binding 0
    glVertexArrayVertexBuffer(gp.vao, 0, gp.vboPos,  0, 3 * sizeof(float));
    glEnableVertexArrayAttrib(gp.vao, 0);
    glVertexArrayAttribFormat(gp.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(gp.vao, 0, 0);
    // normal — binding 1
    glVertexArrayVertexBuffer(gp.vao, 1, gp.vboNorm, 0, 3 * sizeof(float));
    glEnableVertexArrayAttrib(gp.vao, 1);
    glVertexArrayAttribFormat(gp.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(gp.vao, 1, 1);
    // colour — binding 4 (matches preview.vert / obstacle.vert location 4)
    glVertexArrayVertexBuffer(gp.vao, 4, gp.vboCol, 0, 4 * sizeof(float));
    glEnableVertexArrayAttrib(gp.vao, 4);
    glVertexArrayAttribFormat(gp.vao, 4, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(gp.vao, 4, 4);
    // EBO
    glVertexArrayElementBuffer(gp.vao, gp.ebo);
    dbPreviewPrims_.push_back(gp);
  }
}

void EditorApp::dbDrawGripPreview(const ItemDef& d, const glm::mat4& viewProj, float dt) {
  // Lazy-load the player model once.
  if (!playerPreview_.isLoaded() && !playerPreviewTried_) {
    playerPreviewTried_ = true;
    if (playerPreview_.load(resolveFromExe("assets/models/player.glb"))) {
      const int idle = playerPreview_.findClipIndex("Idle_Loop");
      gripClipIndex_ = idle >= 0 ? idle : 0;
      playerPreview_.setClip(playerPreview_.clipName());
    }
  }
  if (!playerPreview_.isLoaded() || !skinnedShader_.isValid()) return;

  // Drive the chosen clip.
  const float clipDur = playerPreview_.clipDuration(gripClipIndex_, 1.0f);
  static float s_t = 0.0f; s_t += dt; if (clipDur > 0 && s_t > clipDur) s_t = std::fmod(s_t, clipDur);

  skinnedShader_.use();
  skinnedShader_.setMat4 ("u_viewProj",        viewProj);
  skinnedShader_.setMat4 ("u_lightViewProj",   glm::mat4(1.f));
  skinnedShader_.setVec3 ("u_lightDir",        glm::vec3(0.3f, -1.f, 0.3f));
  skinnedShader_.setVec3 ("u_paletteLevels",   glm::vec3(0.f));
  skinnedShader_.setFloat("u_paletteEnabled",  0.f);
  skinnedShader_.setFloat("u_ambient",         0.40f);
  skinnedShader_.setFloat("u_diffuse",         0.75f);
  skinnedShader_.setFloat("u_lightingEnabled", 1.f);
  skinnedShader_.setInt  ("u_shadowMap",       1);
  skinnedShader_.setFloat("u_shadowsEnabled",  0.f);
  skinnedShader_.setFloat("u_shadowDarkness",  0.f);
  skinnedShader_.setFloat("u_shadowBias",      0.f);
  skinnedShader_.setFloat("u_fogEnabled",      0.f);
  skinnedShader_.setVec3 ("u_fogColor",        glm::vec3(0.f));
  skinnedShader_.setFloat("u_fogDensity",      0.f);
  skinnedShader_.setFloat("u_fogStart",        0.f);
  skinnedShader_.setVec3 ("u_color",           glm::vec3(0.75f, 0.78f, 0.85f));

  const glm::mat4 playerModel(1.0f);  // player at origin in the preview
  playerPreview_.renderAs(skinnedShader_, playerModel, gripClipIndex_, s_t);

  // Attach the weapon at the current grip — same math as the game client.
  const std::string joint = d.gripJoint.empty()
      ? world::resolveSocketJoint(world::kSocketWeaponMain) : d.gripJoint;
  const int jidx = playerPreview_.findJointIndex(joint);
  if (jidx >= 0 && gripAttach_.valid()) {
    const glm::mat4 grip = world::gripMatrix(
        glm::vec3(d.gripPosX, d.gripPosY, d.gripPosZ),
        glm::vec3(d.gripRotX, d.gripRotY, d.gripRotZ), d.gripScale);
    const glm::mat4 weaponWorld = playerModel * playerPreview_.jointModelMatrix(jidx) * grip;
    gripAttach_.draw(d.modelEquipped, weaponWorld, viewProj);
  }
}

void EditorApp::dbRenderPreview(float dt) {
  if (!dbPreviewFbo_) return;

  dbPreviewAngle_ += dt * 0.8f;
  if (dbPreviewAngle_ > 6.283185f) dbPreviewAngle_ -= 6.283185f;

  glBindFramebuffer(GL_FRAMEBUFFER, dbPreviewFbo_);
  glViewport(0, 0, 256, 256);
  glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Items tab: when the edited item has an equipped model and "preview in hand"
  // is on, show the player holding it (for grip tuning) instead of the spinning
  // weapon. Builds its own camera + restores FBO, then returns.
  if (dbTab_ == 0 && gripPreview_ && !dbEditItem_.modelEquipped.empty()) {
    glEnable(GL_DEPTH_TEST);
    // Frame the player: ~2 units tall, centred at hip height. Orbit is manual
    // (drag on the preview image) — no auto-spin, so grip tweaks are readable.
    const float az = gripYaw_;
    const float elev = gripPitch_;
    const glm::vec3 center(0.0f, 1.0f, 0.0f);
    const float dist = 3.2f;
    const glm::vec3 eye = center + glm::vec3(
        dist * std::cos(az) * std::cos(elev),
        dist * std::sin(elev),
        dist * std::sin(az) * std::cos(elev));
    const glm::mat4 view = glm::lookAtLH(eye, center, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveLH(glm::radians(40.0f), 1.0f, 0.05f, 50.0f);
    dbDrawGripPreview(dbEditItem_, proj * view, dt);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewport3dW_, viewport3dH_);
    return;
  }

  // Common camera setup
  const bool hasContent = dbPreviewHasAnim_ ? dbPreviewSkinned_.isLoaded()
                                            : !dbPreviewPrims_.empty();
  if (hasContent) {
    glEnable(GL_DEPTH_TEST);

    // Orbit camera: fixed 20° elevation, auto-rotating azimuth
    const float camDist = dbPreviewRadius_ * 2.8f;
    const float elevRad = glm::radians(20.0f);
    glm::vec3 eye = dbPreviewCenter_ + glm::vec3(
      camDist * std::cos(dbPreviewAngle_) * std::cos(elevRad),
      camDist * std::sin(elevRad),
      camDist * std::sin(dbPreviewAngle_) * std::cos(elevRad));
    // Left-handed view/projection to match the game (GameCamera::viewProjection
    // uses lookAtLH + perspectiveLH). Using RH here flipped the model upside-down
    // relative to how it renders in-world.
    glm::mat4 view     = glm::lookAtLH(eye, dbPreviewCenter_, glm::vec3(0, 1, 0));
    glm::mat4 proj     = glm::perspectiveLH(glm::radians(40.0f), 1.0f,
                                            camDist * 0.01f, camDist * 4.0f);
    glm::mat4 viewProj = proj * view;

    // Orientation is authored at model-build time now; the preview shows the
    // model exactly as it will appear in-world (no editor rotation applied).
    const glm::mat4 model = glm::mat4(1.0f);

    if (dbPreviewHasAnim_ && skinnedShader_.isValid()) {
      // Animated preview via SkinnedMesh
      dbPreviewSkinned_.update(dt);
      skinnedShader_.use();
      skinnedShader_.setMat4("u_viewProj",      viewProj);
      skinnedShader_.setMat4("u_lightViewProj", glm::mat4(1.f));
      skinnedShader_.setVec3("u_lightDir",      glm::vec3(0.f, -1.f, 0.f));
      skinnedShader_.setVec3("u_paletteLevels", glm::vec3(0.f));
      skinnedShader_.setFloat("u_paletteEnabled",  0.f);
      skinnedShader_.setFloat("u_ambient",         0.35f);
      skinnedShader_.setFloat("u_diffuse",         0.80f);
      skinnedShader_.setFloat("u_lightingEnabled", 1.f);
      skinnedShader_.setInt  ("u_shadowMap",       1);
      skinnedShader_.setFloat("u_shadowsEnabled",  0.f);
      skinnedShader_.setFloat("u_shadowDarkness",  0.f);
      skinnedShader_.setFloat("u_shadowBias",      0.f);
      skinnedShader_.setFloat("u_fogEnabled",      0.f);
      skinnedShader_.setVec3 ("u_fogColor",        glm::vec3(0.f));
      skinnedShader_.setFloat("u_fogDensity",      0.f);
      skinnedShader_.setFloat("u_fogStart",        0.f);
      dbPreviewSkinned_.render(skinnedShader_, model, /*useMaterialColors=*/true);
    } else if (dbPreviewShader_.isValid()) {
      // Static primitive preview
      dbPreviewShader_.use();
      dbPreviewShader_.setMat4("u_model",    model);
      dbPreviewShader_.setMat4("u_viewProj", viewProj);
      for (const auto& prim : dbPreviewPrims_) {
        dbPreviewShader_.setVec4("u_color", prim.color);
        glBindVertexArray(prim.vao);
        glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT, nullptr);
      }
      glBindVertexArray(0);
    }
  }

  // Restore FBO state for the rest of the frame
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, viewport3dW_, viewport3dH_);
}

// -----------------------------------------------------------------------
bool EditorApp::init() {
  if (!window_.init(kInitialWidth, kInitialHeight, kTitle)) return false;

  render::installGlDebugCallback();

  viewport3dFbo_ = std::make_unique<render::MsaaFramebuffer>(
      viewport3dW_, viewport3dH_, kMsaaSamples);

  // ---- Window callbacks ------------------------------------------------
  // Always forward to camera so it always receives press + release pairs.
  window_.onMouseButton = [this](int button, int action, int /*mods*/) {
    camera_.onMouseButton(button, action);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS)   mouseHeld3D_ = true;
      if (action == GLFW_RELEASE) mouseHeld3D_ = false;
    }
  };

  window_.onScroll = [this](double /*x*/, double yoffset) {
    // Ctrl+Scroll → brush size (prevent camera zoom)
    const bool ctrl = glfwGetKey(window_.handle(), GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS
                   || glfwGetKey(window_.handle(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (ctrl) {
      brush_.size = std::clamp(brush_.size + (yoffset > 0 ? 1 : -1), 1, 64);
      return;
    }
    if (!ImGui::GetIO().WantCaptureMouse) camera_.onScroll(yoffset);
  };

  // ---- Shaders ---------------------------------------------------------
  auto loadShader = [](render::Shader& sh, const char* v, const char* f,
                       const char* name) -> bool {
    if (!sh.fromFiles(resolveFromExe(v), resolveFromExe(f))) {
      std::fprintf(stderr, "[Editor] %s shader failed\n", name);
      return false;
    }
    return true;
  };
  if (!loadShader(terrainShader_,         kTerrainVertPath,    kTerrainFragPath,    "terrain"))    return false;
  if (!loadShader(wireframeShader_,       kWireframeVertPath,  kWireframeFragPath,  "wireframe"))  return false;
  if (!loadShader(obstacleShader_,        kObstacleVertPath,   kObstacleFragPath,   "obstacle"))   return false;
  if (!loadShader(skinnedShader_,         kSkinnedVertPath,    kSkinnedFragPath,    "skinned"))    return false;
  if (!loadShader(shadowInstancedShader_, kShadowInstVertPath, kShadowFragPath,     "shadow"))     return false;

  if (!waterRenderer_.init(resolveFromExe(kWaterVertPath).string(),
                            resolveFromExe(kWaterFragPath).string(),
                            resolveFromExe(kWaterNormalPath).string())) {
    std::fprintf(stderr, "[Editor] water renderer init failed\n");
    // Non-fatal: editor still works, water just won't render.
  }

  if (!overlayRenderer_.init(resolveFromExe("shaders/overlay.vert").string(),
                             resolveFromExe("shaders/overlay.frag").string(),
                             resolveFromExe("").string())) {
    std::fprintf(stderr, "[Editor] overlay renderer init failed\n");
    // Non-fatal: editor still works, overlays just won't render.
  }

  // Grip-preview weapon renderer (Items tab) — reuses the preview shader.
  if (!gripAttach_.init(resolveFromExe("shaders/preview.vert").string(),
                        resolveFromExe("shaders/preview.frag").string(),
                        [](const std::string& rel){ return resolveFromExe(rel.c_str()); })) {
    std::fprintf(stderr, "[Editor] grip attachment renderer init failed\n");
  }

  if (!shadowMap_.init(kShadowMapSize)) {
    std::fprintf(stderr, "[Editor] shadow map init failed\n");
    return false;
  }

  obstacles_.initGL();
  walls_.initGL();
  obstacles_.setModelResolver([](const std::string& rel) {
    return resolveFromExe(rel.c_str());
  });
  walls_.setModelResolver([](const std::string& rel) {
    return resolveFromExe(rel.c_str());
  });
  entities_.initGL();

  initNewMap(64, 64);

  camera_.snapTo({ static_cast<float>(map_.width) * 0.5f, 0.0f,
                   static_cast<float>(map_.height) * 0.5f });

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  glDisable(GL_CULL_FACE);

  initHoverMesh();
  initBlockedOverlay();
  initImGui();

  // Load persisted settings if present.
  {
    AppSettings s;
    if (::loadSettings(s, resolveFromExe("settings.cfg"))) {
      fogEnabled_  = s.fogEnabled;   fogDensity_ = s.fogDensity;
      fogStart_    = s.fogStart;     fogColor_   = {s.fogR, s.fogG, s.fogB};
      aoEnabled_   = s.aoEnabled;    aoStrength_ = s.aoStrength;
      lightingEnabled_ = s.lightingEnabled;
      sunYawDeg_ = s.sunYawDeg;  sunPitchDeg_ = s.sunPitchDeg;
      ambient_   = s.ambient;    diffuse_     = s.diffuse;
      shadowsEnabled_  = s.shadowsEnabled;
      shadowHalfExtent_= s.shadowHalfExtent;
      palette_     = s.palette;
      paletteHues_ = s.paletteHues;
      paletteSats_ = s.paletteSats;
      paletteLums_ = s.paletteLums;
      // Water settings (shared with the game client).
      applyWaterSettings(s, waterUniforms_);
      if (!waterUniforms_.causticMapPath.empty())
        waterRenderer_.loadCausticMap(resolveFromExe(waterUniforms_.causticMapPath.c_str()).string());
    }
  }

  // Load recent files list.
  loadRecentFiles();

  // Set initial window title.
  updateWindowTitle();

  // Attempt to pre-populate the entity DB from the server so the Objects
  // toolbar list (PlaceObstacle) shows all registered object types without
  // requiring the user to open Database → Edit Database first.
  // Failure is silently swallowed — the editor still works with the built-in
  // hardcoded fallback list when no server is available.
  try { dbLoadAll(); } catch (...) {}

  lastFrameTime_ = std::chrono::steady_clock::now();
  return true;
}

// -----------------------------------------------------------------------
int EditorApp::run() {
  while (!window_.shouldClose()) {
    window_.pollEvents();

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastFrameTime_).count();
    lastFrameTime_ = now;

    renderFrame(dt);
    window_.swapBuffers();
  }
  return 0;
}

// -----------------------------------------------------------------------
void EditorApp::renderFrame(float dt) {
  GLFWwindow* win = window_.handle();

  // ---- Keyboard shortcuts (non-repeat) ----------------------------------
  {
    const bool ctrl  = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS
                    || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    static bool sZ = false, sY = false, sS = false;

    const bool zNow = (ctrl && glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS);
    if (zNow && !sZ && undo_.canUndo()) {
      const auto& snap = undo_.undo();
      map_ = snap.map; npcSpawns_ = snap.npcs;
      dirty_ = true; updateWindowTitle();
      rebuildTerrainGL(); rebuildObstacles();
      waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
      minimap_.rebuild(map_, npcSpawns_);
    }
    sZ = zNow;

    const bool yNow = (ctrl && glfwGetKey(win, GLFW_KEY_Y) == GLFW_PRESS);
    if (yNow && !sY && undo_.canRedo()) {
      const auto& snap = undo_.redo();
      map_ = snap.map; npcSpawns_ = snap.npcs;
      dirty_ = true; updateWindowTitle();
      rebuildTerrainGL(); rebuildObstacles();
      waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
      minimap_.rebuild(map_, npcSpawns_);
    }
    sY = yNow;

    const bool sNow = (ctrl && glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS);
    if (sNow && !sS) saveCurrentFile();
    sS = sNow;

    // Q / E rotate the placement brush about the up axis. Objects rotate 90°,
    // walls 45° (8 orientations), pillars 90° (4 corners). Q = CCW, E = CW.
    // Suppressed while a text field is focused so typing IDs is safe.
    static bool sQ = false, sE = false;
    const bool typing = ImGui::GetIO().WantTextInput || ctrl;
    const bool qEdge = !typing && glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS && !sQ;
    const bool eEdge = !typing && glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS && !sE;
    sQ = !typing && glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS;
    sE = !typing && glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS;
    if (activeTool_ == EditorTool::PlaceObstacle) {
      if (qEdge) placeRotation_ = (placeRotation_ + 1) & 3;   // CCW
      if (eEdge) placeRotation_ = (placeRotation_ + 3) & 3;   // CW
    } else if (activeTool_ == EditorTool::PlaceWall) {
      if (qEdge) wallOrient_ = (wallOrient_ + 1) & 7;         // 45° CCW
      if (eEdge) wallOrient_ = (wallOrient_ + 7) & 7;         // 45° CW
    } else if (activeTool_ == EditorTool::PlacePillar) {
      if (qEdge) pillarOrient_ = (pillarOrient_ + 2) & 7;     // 90° CCW (corners)
      if (eEdge) pillarOrient_ = (pillarOrient_ + 6) & 7;     // 90° CW
    } else if (activeTool_ == EditorTool::PaintOverlay) {
      if (qEdge) overlayRotation_ = (overlayRotation_ + 3) & 3;  // 90° CCW
      if (eEdge) overlayRotation_ = (overlayRotation_ + 1) & 3;  // 90° CW
    }
  }

  // ---- Camera cursor ----------------------------------------------------
  double cursorX = 0.0, cursorY = 0.0;
  glfwGetCursorPos(win, &cursorX, &cursorY);
  camera_.onCursorPos(cursorX, cursorY);

  // ---- WASD camera pan (suppress only when a text field is active) ----
  // pan() modifies targetPos_. It must be called BEFORE update() and we must
  // pass panTarget() (= targetPos_) into update() — otherwise update() resets
  // targetPos_ = lookAtTarget() = currentTarget_ every frame, discarding the
  // accumulated pan delta before the smoothing step can act on it.
  if (!ImGui::GetIO().WantTextInput) {
    constexpr float kPanSpeed = 30.0f;
    const float panDist = kPanSpeed * dt;
    const bool w = glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS;
    const bool s = glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS;
    const bool a = glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS;
    const bool d = glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS;
    if (w || s || a || d) {
      // Negated: pan() forward/right vectors point camera→target, so flip to
      // get intuitive "W = move forward into the scene" behaviour.
      camera_.pan((a ? panDist : 0.0f) + (d ? -panDist : 0.0f),
                  (s ? panDist : 0.0f) + (w ? -panDist : 0.0f));
    }
  }
  // Pass panTarget() so update()'s "targetPos_ = target" is a no-op and the
  // smoothing correctly chases whatever pan() just wrote.
  camera_.update(dt, win, camera_.panTarget());

  // ---- Tool-change overlay auto-toggle ---------------------------------
  if (activeTool_ != prevTool_) {
    const bool wasBlocked  = (prevTool_ == EditorTool::PaintBlocking);
    const bool wasHeight   = (prevTool_ == EditorTool::SculptTerrain);
    const bool isBlocked   = (activeTool_ == EditorTool::PaintBlocking);
    const bool isHeight    = (activeTool_ == EditorTool::SculptTerrain);

    // Walkability overlay
    if (wasBlocked && overlayWalkabilityAuto_) {
      showWalkabilityOverlay_ = false;
      overlayWalkabilityAuto_ = false;
    }
    if (isBlocked && !showWalkabilityOverlay_) {
      showWalkabilityOverlay_ = true;
      overlayWalkabilityAuto_ = true;
    }

    // Height overlay
    if (wasHeight && overlayHeightAuto_) {
      showHeightOverlay_ = false;
      overlayHeightAuto_ = false;
    }
    if (isHeight && !showHeightOverlay_) {
      showHeightOverlay_ = true;
      overlayHeightAuto_ = true;
    }

    prevTool_ = activeTool_;
  }
  // If user manually enabled an overlay while on the auto-tool, stop tracking it as auto
  if (showWalkabilityOverlay_ && overlayWalkabilityAuto_
      && activeTool_ != EditorTool::PaintBlocking)
    overlayWalkabilityAuto_ = false;
  if (showHeightOverlay_ && overlayHeightAuto_
      && activeTool_ != EditorTool::SculptTerrain)
    overlayHeightAuto_ = false;

  // ---- Pending undo push (after brush stroke ends) ---------------------
  if (undoPending_ && !hadStroke_) {
    undoPending_ = false;
    pushUndo();
  }

  // ---- 3D viewport FBO render (Map workspace only) -----------------------
  if (mode_ == EditorMode::Map) render3DViewport(dt);

  // ---- DB model preview FBO render (runs before ImGui so the texture is ready) --
  if (mode_ == EditorMode::Database && showDbWindow_) dbRenderPreview(dt);

  // ---- ImGui frame -----------------------------------------------------
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ---- Left mode rail (Map / World / Database workspaces) ---------------
  constexpr float kRailW = 64.0f;
  drawModeRail(kRailW);

  // ---- Full-screen DockSpace host window (inset right of the rail) ------
  {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + kRailW, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - kRailW, vp->WorkSize.y));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar      | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize        | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar         | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(kDockSpaceWindowName, nullptr, hostFlags);
    ImGui::PopStyleVar(2);

    // Menu bar inside host window
    if (ImGui::BeginMenuBar()) { drawMenuBar(); ImGui::EndMenuBar(); }

    // Set up initial dock layout once
    ImGuiID dsId = ImGui::GetID(kDockSpaceName);
    ImGuiDockNodeFlags dsFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dsId, ImVec2(0, 0), dsFlags);

    // Build the default layout only when no saved layout exists (first ever
    // run, or after "Reset Layout").  DockBuilderGetNode returns non-null once
    // imgui.ini has been loaded/saved, so we don't clobber user arrangements.
    const bool needDefaultLayout = (ImGui::DockBuilderGetNode(dsId) == nullptr);
    if (needDefaultLayout || resetLayout_) {
      resetLayout_ = false;
      ImGui::DockBuilderRemoveNode(dsId);
      ImGui::DockBuilderAddNode(dsId, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dsId, vp->WorkSize);

      // Split: left narrow toolbar | center | right narrow props
      ImGuiID toolbarId, afterToolbar;
      ImGui::DockBuilderSplitNode(dsId, ImGuiDir_Left, 130.0f / vp->WorkSize.x,
                                  &toolbarId, &afterToolbar);
      ImGuiID propsId, center;
      ImGui::DockBuilderSplitNode(afterToolbar, ImGuiDir_Right, 220.0f / (vp->WorkSize.x - 130.0f),
                                  &propsId, &center);
      // Split center: 3D left ~55%, 2D right ~45%
      ImGuiID vpId, gridId;
      ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.55f, &vpId, &gridId);

      ImGui::DockBuilderDockWindow(kToolbarName,    toolbarId);
      ImGui::DockBuilderDockWindow(kPropsName,      propsId);
      ImGui::DockBuilderDockWindow(kViewport3dName, vpId);
      ImGui::DockBuilderDockWindow(kGridName,       gridId);
      ImGui::DockBuilderFinish(dsId);
    }

    ImGui::End();
  }

  // ---- Workspace content --------------------------------------------------
  // Each mode owns the content area right of the rail. World and Database fill
  // it with a single fixed window; Map uses the docked multi-window layout.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float menuH = ImGui::GetFrameHeight();
  const ImVec2 wsPos (vp->WorkPos.x + kRailW, vp->WorkPos.y + menuH);
  const ImVec2 wsSize(vp->WorkSize.x - kRailW, vp->WorkSize.y - menuH);

  switch (mode_) {
    case EditorMode::Map:
      drawToolbar();
      drawProperties();
      drawPreferencesWindow();
      draw3DViewportWindow();
      drawGridView();
      drawMinimapWindow();
      break;
    case EditorMode::World:
      ImGui::SetNextWindowPos(wsPos,  ImGuiCond_Always);
      ImGui::SetNextWindowSize(wsSize, ImGuiCond_Always);
      drawWorldView();
      break;
    case EditorMode::Database:
      ImGui::SetNextWindowPos(wsPos,  ImGuiCond_Always);
      ImGui::SetNextWindowSize(wsSize, ImGuiCond_Always);
      drawDatabaseWindow();   // fills the content area; rail switches away
      break;
  }

  // ---- Dialogs ----------------------------------------------------------
  if (showNewMapDialog_) { ImGui::OpenPopup("New Map"); showNewMapDialog_ = false; }
  if (ImGui::BeginPopupModal("New Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Map size:");
    ImGui::InputInt("Width##nm",  &newMapW_);
    ImGui::InputInt("Height##nm", &newMapH_);
    newMapW_ = std::clamp(newMapW_, 8, 256);
    newMapH_ = std::clamp(newMapH_, 8, 256);
    if (ImGui::Button("Create", ImVec2(80, 0))) {
      initNewMap(newMapW_, newMapH_);
      currentFilePath_.clear();
      dirty_ = false;
      updateWindowTitle();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (showResizeDialog_) { ImGui::OpenPopup("Resize Map"); showResizeDialog_ = false; }
  if (ImGui::BeginPopupModal("Resize Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("New size (crop/pad):");
    ImGui::InputInt("Width##rs",  &resizeW_);
    ImGui::InputInt("Height##rs", &resizeH_);
    resizeW_ = std::clamp(resizeW_, 8, 256);
    resizeH_ = std::clamp(resizeH_, 8, 256);
    if (ImGui::Button("Apply", ImVec2(80, 0))) { resizeMap(resizeW_, resizeH_); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ---- Present ----------------------------------------------------------
  const int fbW = window_.framebufferWidth();
  const int fbH = window_.framebufferHeight();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbW, fbH);
  glClearColor(0.08f, 0.05f, 0.02f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  hadStroke_ = false;
}

// -----------------------------------------------------------------------
void EditorApp::render3DViewport(float dt) {
  const float aspect = (viewport3dH_ > 0)
    ? static_cast<float>(viewport3dW_) / static_cast<float>(viewport3dH_)
    : 1.0f;
  const glm::mat4 viewProj = camera_.viewProjection(aspect);
  const glm::vec3 sunDir   = sunDirectionFromYawPitch(sunYawDeg_, sunPitchDeg_);
  const glm::vec3 mapCenter = { static_cast<float>(map_.width) * 0.5f, 0.0f,
                                 static_cast<float>(map_.height) * 0.5f };
  const glm::mat4 lightVP  = render::ShadowMap::lightViewProj(sunDir, mapCenter, shadowHalfExtent_);

  if (shadowsEnabled_) {
    shadowMap_.beginPass();
    shadowInstancedShader_.use();
    shadowInstancedShader_.setMat4("u_lightViewProj", lightVP);
    obstacles_.renderDepth(shadowInstancedShader_);
    shadowMap_.endPass();
  }

  viewport3dFbo_->bind();
  glViewport(0, 0, viewport3dW_, viewport3dH_);
  glClearColor(0.45f, 0.65f, 0.85f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  glBindTextureUnit(1, shadowMap_.depthTexture());

  // Terrain
  terrainShader_.use();
  terrainShader_.setInt  ("u_shadowMap",       1);
  terrainShader_.setMat4 ("u_lightViewProj",   lightVP);
  terrainShader_.setFloat("u_shadowsEnabled",  shadowsEnabled_ ? 1.0f : 0.0f);
  terrainShader_.setFloat("u_shadowDarkness",  0.35f);
  terrainShader_.setFloat("u_shadowBias",      0.0005f);
  terrainShader_.setMat4 ("u_viewProj",        viewProj);
  terrainShader_.setVec3 ("u_paletteLevels",   glm::vec3(static_cast<float>(paletteHues_),
                                                          static_cast<float>(paletteSats_),
                                                          static_cast<float>(paletteLums_)));
  terrainShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
  terrainShader_.setVec3 ("u_lightDir",        sunDir);
  terrainShader_.setFloat("u_ambient",         ambient_);
  terrainShader_.setFloat("u_diffuse",         diffuse_);
  terrainShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
  terrainShader_.setFloat("u_fogEnabled",  fogEnabled_  ? 1.0f : 0.0f);
  terrainShader_.setVec3 ("u_fogColor",    fogColor_);
  terrainShader_.setFloat("u_fogDensity",  fogDensity_);
  terrainShader_.setFloat("u_fogStart",    fogStart_);
  terrainShader_.setFloat("u_aoEnabled",   aoEnabled_   ? 1.0f : 0.0f);
  terrainShader_.setFloat("u_aoStrength",  aoStrength_);
  terrainMesh_.draw();

  // Neighbor-chunk ghosts (read-only seam-authoring preview). World offset and
  // dimmed colours are baked into each mesh, so the terrain shader needs no
  // extra uniforms.
  for (const auto& np : neighbors_)
    if (np.mesh.isValid()) np.mesh.draw();

  // Overlay surfaces (paths / floors / shaped ground). Rebuild lazily when the
  // overlayTiles signature changes (covers paint, undo/redo, load, resize).
  if (overlayRenderer_.valid()) {
    std::size_t h = map_.overlayTiles.size() * 1469598103934665603ull;
    for (const auto& o : map_.overlayTiles)
      h = (h ^ (static_cast<std::size_t>(o.tileX) * 73856093u ^
                static_cast<std::size_t>(o.tileY) * 19349663u ^
                static_cast<std::size_t>(o.shape) * 83492791u ^
                static_cast<std::size_t>(o.materialId) * 2654435761u)) * 1099511628211ull;
    // Fold terrain heights into the signature so overlays re-drape smoothly
    // when the user sculpts/raises/lowers the ground beneath them.
    for (float vhgt : map_.vertexHeights)
      h = (h ^ static_cast<std::size_t>(std::bit_cast<std::uint32_t>(vhgt))) * 1099511628211ull;
    if (h != overlayHash_) { overlayRenderer_.rebuild(map_); overlayHash_ = h; }

    world::OverlayLighting ol;
    ol.viewProj        = viewProj;
    ol.lightViewProj   = lightVP;
    ol.lightDir        = sunDir;
    ol.paletteLevels   = glm::vec3(static_cast<float>(paletteHues_),
                                   static_cast<float>(paletteSats_),
                                   static_cast<float>(paletteLums_));
    ol.paletteEnabled  = palette_ ? 1.0f : 0.0f;
    ol.ambient         = ambient_;
    ol.diffuse         = diffuse_;
    ol.lightingEnabled = lightingEnabled_ ? 1.0f : 0.0f;
    ol.shadowsEnabled  = shadowsEnabled_  ? 1.0f : 0.0f;
    ol.shadowDarkness  = 0.35f;
    ol.shadowBias      = 0.0005f;
    ol.fogEnabled      = fogEnabled_ ? 1.0f : 0.0f;
    ol.fogColor        = fogColor_;
    ol.fogDensity      = fogDensity_;
    ol.fogStart        = fogStart_;
    ol.shadowMapUnit   = 1;
    overlayRenderer_.render(ol);
  }

  // Obstacles
  obstacleShader_.use();
  obstacleShader_.setMat4 ("u_viewProj",       viewProj);
  obstacleShader_.setVec3 ("u_lightDir",       sunDir);
  obstacleShader_.setVec3 ("u_paletteLevels",  glm::vec3(static_cast<float>(paletteHues_),
                                                           static_cast<float>(paletteSats_),
                                                           static_cast<float>(paletteLums_)));
  obstacleShader_.setFloat("u_paletteEnabled", palette_ ? 1.0f : 0.0f);
  obstacleShader_.setFloat("u_ambient",        ambient_);
  obstacleShader_.setFloat("u_diffuse",        diffuse_);
  obstacleShader_.setFloat("u_lightingEnabled",lightingEnabled_ ? 1.0f : 0.0f);
  obstacleShader_.setFloat("u_fogEnabled", fogEnabled_  ? 1.0f : 0.0f);
  obstacleShader_.setVec3 ("u_fogColor",   fogColor_);
  obstacleShader_.setFloat("u_fogDensity", fogDensity_);
  obstacleShader_.setFloat("u_fogStart",   fogStart_);
  obstacles_.render(obstacleShader_);  // all static objects (data-driven)
  walls_.render(obstacleShader_);      // wall + pillar placeholders

  // NPCs — same data-driven models as the game (placeholder when no model).
  {
    std::vector<world::EntityRenderer::Instance> insts;
    std::vector<std::string> kinds;
    insts.reserve(npcSpawns_.size());
    kinds.reserve(npcSpawns_.size());
    for (const auto& n : npcSpawns_) {
      const float wy = tileWorldY(n.tileX, n.tileY);
      insts.push_back({ static_cast<float>(n.tileX), wy, static_cast<float>(n.tileY), 0.0f });
      kinds.push_back(n.kind);
    }
    entities_.setNpcInstances(insts, kinds);
    entities_.render(obstacleShader_);  // static NPC models
  }

  // ---- Fishing spot animated model — OPAQUE pass -----------------------------
  // Rendered with the opaque scene BEFORE the SSR snapshot so the water shader
  // captures it in sceneColor and draws it as a refracted underwater object.
  if (obstacles_.hasCustomModels() || entities_.hasAnimatedNpcs()) {
    skinnedShader_.use();
    skinnedShader_.setMat4 ("u_viewProj",       viewProj);
    skinnedShader_.setMat4 ("u_lightViewProj",  lightVP);
    skinnedShader_.setVec3 ("u_lightDir",       sunDir);
    skinnedShader_.setVec3 ("u_paletteLevels",  glm::vec3(static_cast<float>(paletteHues_),
                                                           static_cast<float>(paletteSats_),
                                                           static_cast<float>(paletteLums_)));
    skinnedShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
    skinnedShader_.setFloat("u_ambient",         ambient_);
    skinnedShader_.setFloat("u_diffuse",         diffuse_);
    skinnedShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
    skinnedShader_.setInt  ("u_shadowMap",       1);
    skinnedShader_.setFloat("u_shadowsEnabled",  0.0f);
    skinnedShader_.setFloat("u_shadowDarkness",  0.0f);
    skinnedShader_.setFloat("u_shadowBias",      0.0f);
    skinnedShader_.setFloat("u_fogEnabled",      fogEnabled_ ? 1.0f : 0.0f);
    skinnedShader_.setVec3 ("u_fogColor",        fogColor_);
    skinnedShader_.setFloat("u_fogDensity",      fogDensity_);
    skinnedShader_.setFloat("u_fogStart",        fogStart_);

    // Data-driven animated custom objects (incl. fishing_spot) + NPCs.
    obstacles_.renderCustomAnimated(skinnedShader_, dt);
    entities_.renderAnimatedNpcs(skinnedShader_, dt);  // animated NPCs in editor
  }

  // ---- Placement ghost preview --------------------------------------------
  // While the Objects tool is active and a tile is hovered, draw the selected
  // object's model translucent at the cursor so its footprint/orientation is
  // clear before placing. Constant-alpha blend = no shader change needed.
  if (activeTool_ == EditorTool::PlaceObstacle && hoveredTileX_ >= 0 &&
      !obstacleSubtype_.empty()) {
    glEnable(GL_BLEND);
    glBlendColor(0.f, 0.f, 0.f, 0.5f);            // 50% ghost transparency
    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    glDepthMask(GL_FALSE);                          // don't pollute depth
    glDepthFunc(GL_LEQUAL);
    // obstacleShader_ + skinnedShader_ already have this frame's uniforms set.
    obstacleShader_.use();
    obstacles_.renderGhostAt(obstacleShader_, skinnedShader_, map_,
                             obstacleSubtype_, hoveredTileX_, hoveredTileY_,
                             placeRotation_);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   // restore default
    glDisable(GL_BLEND);
  }

  // Wall / pillar placement ghost (same translucent treatment).
  if ((activeTool_ == EditorTool::PlaceWall || activeTool_ == EditorTool::PlacePillar) &&
      hoveredTileX_ >= 0) {
    const bool pillar = (activeTool_ == EditorTool::PlacePillar);
    const int  orient = pillar ? pillarOrient_ : wallOrient_;
    const std::string& objId = pillar ? pillarSubtype_ : wallSubtype_;
    glEnable(GL_BLEND);
    glBlendColor(0.f, 0.f, 0.f, 0.5f);
    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    obstacleShader_.use();
    walls_.renderGhostAt(obstacleShader_, map_, hoveredTileX_, hoveredTileY_, orient, pillar, objId);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
  }

  // ---- Water pass -------------------------------------------------------
  // Resolve colour + depth (full opaque scene incl. submerged fish), then draw
  // depth-based refraction water on top.
  if (mapHasWater(map_) && waterRenderer_.valid()) {
    viewport3dFbo_->resolve();
    viewport3dFbo_->resolveDepth();

    viewport3dFbo_->bind();
    glViewport(0, 0, viewport3dW_, viewport3dH_);

    waterUniforms_.cameraPos = camera_.cameraPosition();
    waterUniforms_.sunDir    = sunDir;
    waterUniforms_.nearPlane = 0.1f;    // matches GameCamera::viewProjection
    waterUniforms_.farPlane  = 500.0f;
    waterRenderer_.render(
        static_cast<float>(glfwGetTime()),
        viewProj,
        viewport3dFbo_->resolveColorTexture(),
        viewport3dFbo_->resolveDepthTexture(),
        waterUniforms_);
  }

  // ---- Wireframe overlay — AFTER water so it composites on top ----------
  if (showWireframe_) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color", glm::vec4(0.0f, 0.0f, 0.0f, 0.30f));
    terrainMesh_.draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  // ---- Hover outline (yellow) — AFTER water so it renders on top -------
  if (hoveredTileX_ >= 0) {
    updateHoverMesh(hoveredTileX_, hoveredTileY_, brush_.size, brush_.size);
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color",   glm::vec4(1.0f, 0.85f, 0.10f, 1.0f));
    glDepthMask(GL_FALSE);
    glBindVertexArray(hoverVao_);
    if (hoverIsRound_)
      glDrawArrays(GL_LINES,     0, static_cast<GLsizei>(hoverVertCount_));
    else
      glDrawArrays(GL_LINE_LOOP, 0, 4);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
  }

  // ---- Blocked-tile X overlay — AFTER water so Xs render over water ----
  if (showWalkabilityOverlay_) {
    rebuildBlockedOverlay();
    if (blockedLineCount_ > 0) {
      wireframeShader_.use();
      wireframeShader_.setMat4("u_viewProj", viewProj);
      wireframeShader_.setVec4("u_color",   glm::vec4(0.95f, 0.15f, 0.15f, 1.0f));
      glDepthMask(GL_FALSE);
      glBindVertexArray(blockedVao_);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(blockedLineCount_));
      glBindVertexArray(0);
      glDepthMask(GL_TRUE);
    }
  }

  viewport3dFbo_->resolve();   // final resolve for ImGui display
  (void)dt;
}

// -----------------------------------------------------------------------
void EditorApp::setMode(EditorMode m) {
  if (mode_ == m) return;
  mode_ = m;
  if (m == EditorMode::Database) {
    showDbWindow_ = true;
    if (!dbLoaded_) dbLoadAll();
  }
  if (m == EditorMode::World) showWorldView_ = true;
}

void EditorApp::drawModeRail(float railW) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(railW, vp->WorkSize.y));
  ImGui::SetNextWindowViewport(vp->ID);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 8));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4, 6));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(22, 24, 28, 255));
  ImGui::Begin("##moderail", nullptr, flags);

  const float btnSide = railW - 8.0f;
  auto modeBtn = [&](const char* label, EditorMode m, const char* tip) {
    const bool active = (mode_ == m);
    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(70, 110, 180, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(85, 125, 195, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(60, 100, 170, 255));
    }
    if (ImGui::Button(label, ImVec2(btnSide, btnSide))) setMode(m);
    if (active) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };

  modeBtn("Map",   EditorMode::Map,      "Map Editor — edit the open chunk map");
  modeBtn("World", EditorMode::World,    "World Editor — assign chunk maps to the world grid");
  modeBtn("DB",    EditorMode::Database, "Database — items, NPCs, objects, actions, skills");

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
}

void EditorApp::drawMenuBar() {
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Map...", "Ctrl+N")) showNewMapDialog_ = true;
    if (ImGui::MenuItem("Open...",   "Ctrl+O")) openFileDialog();
    // Open Recent submenu
    const bool hasRecent = !recentFiles_.empty();
    if (ImGui::BeginMenu("Open Recent", hasRecent)) {
      for (int rfi = 0; rfi < static_cast<int>(recentFiles_.size()); ++rfi) {
        const auto& rf = recentFiles_[rfi];
        ImGui::PushID(rfi);
        const std::string label = std::filesystem::path(rf).filename().string();
        if (ImGui::MenuItem(label.c_str())) openRecentFile(rf);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rf.c_str());
        ImGui::PopID();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Clear Recent")) {
        recentFiles_.clear();
        saveRecentFiles();
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save",      "Ctrl+S")) saveCurrentFile();
    if (ImGui::MenuItem("Save As..."))          saveAsDialog();
    ImGui::Separator();
    if (ImGui::MenuItem("Exit"))
      glfwSetWindowShouldClose(window_.handle(), GLFW_TRUE);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undo_.canUndo())) {
      const auto& s = undo_.undo(); map_ = s.map; npcSpawns_ = s.npcs;
      dirty_ = true; updateWindowTitle();
      rebuildTerrainGL(); rebuildObstacles();
      waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
      minimap_.rebuild(map_, npcSpawns_);
    }
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undo_.canRedo())) {
      const auto& s = undo_.redo(); map_ = s.map; npcSpawns_ = s.npcs;
      dirty_ = true; updateWindowTitle();
      rebuildTerrainGL(); rebuildObstacles();
      waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
      minimap_.rebuild(map_, npcSpawns_);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Preferences...")) showPrefsWindow_ = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Map")) {
    if (ImGui::MenuItem("Resize...")) { resizeW_ = map_.width; resizeH_ = map_.height; showResizeDialog_ = true; }
    ImGui::EndMenu();
  }
  // World/Database mode switching lives on the left rail; the menus keep only
  // the world-manifest file actions (no nav duplication).
  if (ImGui::BeginMenu("World")) {
    if (ImGui::MenuItem("New World"))       { worldNewManifest(); setMode(EditorMode::World); }
    if (ImGui::MenuItem("Open World..."))   { worldOpenManifest(); setMode(EditorMode::World); }
    if (ImGui::MenuItem("Save World", nullptr, false, !worldManifest_.chunks.empty() || worldDirty_))
      worldSaveManifest();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Height Overlay",      nullptr, &showHeightOverlay_))
      overlayHeightAuto_ = false;
    if (ImGui::MenuItem("Walkability Overlay", nullptr, &showWalkabilityOverlay_))
      overlayWalkabilityAuto_ = false;
    ImGui::MenuItem("Gridmap Overlay",         nullptr, &showGridmapOverlay_);
    ImGui::MenuItem("Wireframe",               nullptr, &showWireframe_);
    ImGui::Separator();
    ImGui::MenuItem("Palette Quantisation",    nullptr, &palette_);
    ImGui::MenuItem("Lighting",                nullptr, &lightingEnabled_);
    ImGui::MenuItem("Shadows",                 nullptr, &shadowsEnabled_);
    ImGui::Separator();
    if (ImGui::MenuItem("Save Layout as Default"))
      ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    if (ImGui::MenuItem("Reset Layout"))
      resetLayout_ = true;
    ImGui::EndMenu();
  }
}

// -----------------------------------------------------------------------
void EditorApp::drawToolbar() {
  ImGui::Begin(kToolbarName);

  auto toolBtn = [&](const char* label, EditorTool t) {
    const bool active = (activeTool_ == t);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
    if (ImGui::Button(label, ImVec2(-1, 0))) activeTool_ = t;
    if (active) ImGui::PopStyleColor();
  };

  ImGui::TextDisabled("-- Tools --");
  toolBtn("Paint",     EditorTool::PaintTerrain);
  toolBtn("Terrain",   EditorTool::SculptTerrain);
  toolBtn("Flatten",   EditorTool::FlattenTerrain);
  toolBtn("Objects",   EditorTool::PlaceObstacle);
  toolBtn("Wall",      EditorTool::PlaceWall);
  toolBtn("Pillar",    EditorTool::PlacePillar);
  toolBtn("NPC",       EditorTool::PlaceNPC);
  toolBtn("Spawn",     EditorTool::PlaceSpawn);
  toolBtn("Collision", EditorTool::PaintBlocking);
  toolBtn("Overlay",   EditorTool::PaintOverlay);

  ImGui::Separator();
  ImGui::TextDisabled("-- Brush --");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderInt("##sz", &brush_.size, 1, 32, "Size:%d");
  brush_.size = std::clamp(brush_.size, 1, 64);

  bool isRound = (brush_.shape == BrushShape::Round);
  if (ImGui::Checkbox("Round", &isRound))
    brush_.shape = isRound ? BrushShape::Round : BrushShape::Square;

  if (activeTool_ == EditorTool::SculptTerrain) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##str", &brush_.strength, 0.01f, 0.5f, "Str:%.2f");
  }
  if (activeTool_ == EditorTool::FlattenTerrain) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##str", &brush_.strength, 0.01f, 0.5f, "Pull:%.2f");
    ImGui::TextDisabled("Levels brush area");
    ImGui::TextDisabled("to its avg height");
  }

  ImGui::Separator();
  ImGui::TextDisabled("LMB = primary");
  ImGui::TextDisabled("RMB = secondary");
  ImGui::Separator();
  ImGui::TextDisabled("Ctrl+Scroll");
  ImGui::TextDisabled("= brush size");
  ImGui::Separator();
  ImGui::TextDisabled("WASD = pan cam");

  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::drawWaterSettings() {
  auto& u = waterUniforms_;

  // ---- Waves -------------------------------------------------------------
  if (ImGui::CollapsingHeader("Waves", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##wsp",  &u.waveSpeed,      0.0f, 2.0f,  "Speed:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##wht",  &u.waveHeight,     0.0f, 0.5f,  "Height:%.3f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##wsc",  &u.waveScale,      0.5f, 8.0f,  "Scale:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##nstr", &u.normalStrength, 0.0f, 2.0f,  "NormalStr:%.2f");
  }

  // ---- Colour & Depth ----------------------------------------------------
  if (ImGui::CollapsingHeader("Colour & Depth", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SetNextItemWidth(-1); ImGui::ColorEdit3("Shallow##w", &u.shallowColor.x);
    ImGui::SetNextItemWidth(-1); ImGui::ColorEdit3("Deep##w",    &u.deepColor.x);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##dfade", &u.depthFade,   0.5f, 20.0f, "DepthFade:%.1f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##shdep", &u.shoreDepth,  0.0f, 1.0f,  "ShoreDepth:%.2f");
    float prevOff = u.waterOffset;
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##woff",  &u.waterOffset, 0.0f, 0.5f,  "WaterLevel:%.3f");
    if (u.waterOffset != prevOff)  // rebuild mesh — water Y changed
      waterRenderer_.rebuild(map_, u.waterOffset);
  }

  // ---- Refraction (underwater view) --------------------------------------
  if (ImGui::CollapsingHeader("Refraction", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rfl",  &u.reflectStrength,    0.0f, 1.0f,  "Clarity:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##refr", &u.refractionStrength, 0.0f, 0.15f, "Distortion:%.3f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##prlx", &u.parallaxDepth,      0.0f, 0.15f, "Parallax:%.3f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##spec", &u.specularStrength,   0.0f, 2.0f,  "Sparkle:%.2f");
  }

  // ---- Foam --------------------------------------------------------------
  if (ImGui::CollapsingHeader("Foam", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SetNextItemWidth(-1); ImGui::ColorEdit3("Colour##foam", &u.foamColor.x);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##cfoam",&u.foamContactWidth, 0.0f, 2.0f,  "ContactWidth:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##fwid", &u.foamWidth,        0.0f, 1.0f,  "Amount:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##fspd", &u.foamSpeed,        0.0f, 2.0f,  "Speed:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##fsc",  &u.foamScale,        1.0f, 20.0f, "Scale:%.1f");
  }

  // ---- Caustics ----------------------------------------------------------
  if (ImGui::CollapsingHeader("Caustics")) {
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##caus", &u.causticIntensity, 0.0f, 1.0f,  "Intensity:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##csc",  &u.causticScale,     0.0f, 12.0f, "Scale:%.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##cspd", &u.causticSpeed,     0.0f, 1.0f,  "Speed:%.2f");

    ImGui::Spacing();
    if (ImGui::Button("Load Caustic Map...", ImVec2(-1, 0))) {
      const std::wstring wpath = winOpenDialog();
      if (!wpath.empty()) {
        // Convert wide path to narrow UTF-8 string.
        const int sz = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
        std::string srcPath(static_cast<std::size_t>(sz), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1,
                            srcPath.data(), sz, nullptr, nullptr);
        if (!srcPath.empty() && srcPath.back() == '\0') srcPath.pop_back();

        // Copy into the shared assets folder under a fixed name so both the
        // editor and the game client (same Release dir) can reload it, and
        // record the relative path so it persists in settings.cfg.
        const std::string relPath = "assets/water_caustic.png";
        const auto destPath = resolveFromExe(relPath.c_str());
        std::error_code ec;
        std::filesystem::create_directories(destPath.parent_path(), ec);
        std::filesystem::copy_file(srcPath, destPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        const std::string loadFrom = ec ? srcPath : destPath.string();
        if (waterRenderer_.loadCausticMap(loadFrom))
          waterUniforms_.causticMapPath = ec ? srcPath : relPath;
      }
    }
    if (!waterUniforms_.causticMapPath.empty()) {
      ImGui::TextDisabled("Loaded: %s", waterUniforms_.causticMapPath.c_str());
      if (ImGui::Button("Clear Caustic Map", ImVec2(-1, 0)))
        waterUniforms_.causticMapPath.clear();  // procedural fallback resumes next launch
    } else {
      ImGui::TextDisabled("(none — using procedural)");
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled("Save via 'Save as Default'");
}

// -----------------------------------------------------------------------
void EditorApp::drawProperties() {
  ImGui::Begin(kPropsName);

  if (activeTool_ == EditorTool::PaintTerrain) {
    ImGui::TextDisabled("Palette");
    float col[3] = { paletteR_, paletteG_, paletteB_ };

    for (int i = 0; i < static_cast<int>(kPaletteSwatches.size()); ++i) {
      if (i % 4 != 0) ImGui::SameLine(0, 2);
      float sr = 0, sg = 0, sb = 0;
      hexToRgbf(kPaletteSwatches[i], sr, sg, sb);
      ImGui::PushID(i);
      const bool sel = (std::abs(sr - paletteR_) < 0.01f &&
                        std::abs(sg - paletteG_) < 0.01f &&
                        std::abs(sb - paletteB_) < 0.01f);
      if (sel) ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
      if (ImGui::ColorButton("##sw", ImVec4(sr, sg, sb, 1.0f),
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                             ImVec2(40, 20))) {
        paletteR_ = sr; paletteG_ = sg; paletteB_ = sb;
      }
      if (sel) ImGui::PopStyleVar();
      ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::ColorPicker3("##picker", col,
                            ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_PickerHueBar)) {
      paletteR_ = col[0]; paletteG_ = col[1]; paletteB_ = col[2];
    }
  }
  else if (activeTool_ == EditorTool::PlaceObstacle) {
    ImGui::TextDisabled("Object type");
    // Scrollable object list — DB-driven when loaded, hardcoded fallback otherwise.
    // obstacleSubtype_ is now a plain string ID (e.g. "tree", "bookcase_oak").
    auto obstBtn = [&](const char* label, const std::string& id) {
      const bool a = (obstacleSubtype_ == id);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
      if (ImGui::Button(label, ImVec2(-1, 0))) obstacleSubtype_ = id;
      if (a) ImGui::PopStyleColor();
    };
    if (!dbObjects_.empty()) {
      // DB-driven grouped list
      for (const auto& obj : dbObjects_) {
        obstBtn(obj.name.c_str(), obj.id);
      }
    } else {
      // Fallback built-ins when DB not loaded
      obstBtn("Tree",         "tree");
      obstBtn("Rock",         "rock");
      obstBtn("Chest",        "chest");
      obstBtn("Fence",        "fence");
      obstBtn("Fishing Spot", "fishing_spot");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Rotation (Q / E)");
    ImGui::Text("%d\xC2\xB0", (placeRotation_ & 3) * 90);
    ImGui::SameLine();
    if (ImGui::SmallButton("Q -90")) placeRotation_ = (placeRotation_ + 1) & 3;
    ImGui::SameLine();
    if (ImGui::SmallButton("E +90")) placeRotation_ = (placeRotation_ + 3) & 3;
  }
  else if (activeTool_ == EditorTool::PlaceWall) {
    static const char* kDir[8] = { "N","NE","E","SE","S","SW","W","NW" };
    ImGui::TextDisabled("Wall variant");
    auto wallBtn = [&](const char* label, const std::string& id) {
      const bool a = (wallSubtype_ == id);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
      if (ImGui::Button(label, ImVec2(-1, 0))) wallSubtype_ = id;
      if (a) ImGui::PopStyleColor();
    };
    wallBtn("Placeholder", "wall");
    for (const auto& o : dbObjects_)
      if (o.objectType == "Wall") wallBtn(o.name.c_str(), o.id);
    ImGui::Separator();
    ImGui::Text("Orient: %s (%d\xC2\xB0)", kDir[wallOrient_ & 7], (wallOrient_ & 7) * 45);
    if (ImGui::SmallButton("Q -45##wall")) wallOrient_ = (wallOrient_ + 1) & 7;
    ImGui::SameLine();
    if (ImGui::SmallButton("E +45##wall")) wallOrient_ = (wallOrient_ + 7) & 7;
    ImGui::TextDisabled("Even = edge wall, odd = diagonal.");
    ImGui::TextDisabled("Left-click place, right-click remove.");
  }
  else if (activeTool_ == EditorTool::PlacePillar) {
    static const char* kCorner[4] = { "NE", "SE", "SW", "NW" };
    ImGui::TextDisabled("Pillar variant");
    auto pilBtn = [&](const char* label, const std::string& id) {
      const bool a = (pillarSubtype_ == id);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
      if (ImGui::Button(label, ImVec2(-1, 0))) pillarSubtype_ = id;
      if (a) ImGui::PopStyleColor();
    };
    pilBtn("Placeholder", "pillar");
    for (const auto& o : dbObjects_)
      if (o.objectType == "Pillar") pilBtn(o.name.c_str(), o.id);
    ImGui::Separator();
    ImGui::Text("Corner: %s", kCorner[(pillarOrient_ & 7) / 2]);
    if (ImGui::SmallButton("Q##pillar")) pillarOrient_ = (pillarOrient_ + 2) & 7;
    ImGui::SameLine();
    if (ImGui::SmallButton("E##pillar")) pillarOrient_ = (pillarOrient_ + 6) & 7;
    ImGui::TextDisabled("Left-click place, right-click remove.");
  }
  else if (activeTool_ == EditorTool::PlaceNPC) {
    ImGui::TextDisabled("NPC type");
    if (!dbNPCs_.empty()) {
      // DB-driven list
      for (const auto& npc : dbNPCs_) {
        const bool a = (npcSubtype_ == npc.id);
        if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
        if (ImGui::Button(npc.name.c_str(), ImVec2(-1, 0))) npcSubtype_ = npc.id;
        if (a) ImGui::PopStyleColor();
      }
    } else {
      // Fallback when DB not loaded
      auto npcBtn = [&](const char* label) {
        const bool a = (npcSubtype_ == label);
        if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
        if (ImGui::Button(label, ImVec2(-1, 0))) npcSubtype_ = label;
        if (a) ImGui::PopStyleColor();
      };
      npcBtn("chicken");
      npcBtn("shopkeeper");
    }
  }
  else if (activeTool_ == EditorTool::PaintOverlay) {
    ImGui::TextDisabled("Material");
    const auto& mats = world::overlayMaterials();
    for (int i = 1; i < static_cast<int>(mats.size()); ++i) {  // skip 0 = none
      const bool a = (overlayMaterial_ == i);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
      if (ImGui::Button(mats[static_cast<std::size_t>(i)].name.c_str(), ImVec2(-1, 0)))
        overlayMaterial_ = i;
      if (a) ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Shape");
    // 12 shape previews in a 4-column grid. White = overlay coverage, dark =
    // underlay (terrain) showing through. Click to select.
    const auto& shapes = world::overlayShapeTriangles();
    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    const float  cell = 40.0f;
    const ImU32  cOverlay = IM_COL32(235, 235, 235, 255);
    const ImU32  cUnder   = IM_COL32(40, 70, 40, 255);
    const ImU32  cSel     = IM_COL32(230, 170, 40, 255);
    for (int s = 0; s < world::kNumOverlayShapes; ++s) {
      if (s % 4 != 0) ImGui::SameLine();
      ImGui::PushID(s);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##shape", ImVec2(cell, cell));
      if (ImGui::IsItemClicked()) overlayShape_ = s;
      const ImVec2 p1 = ImVec2(p0.x + cell, p0.y + cell);
      dl->AddRectFilled(p0, p1, cUnder);
      // v=1 (north) maps to top (p0.y); v=0 (south) maps to bottom (p1.y).
      // Apply the active rotation so the preview reflects what gets painted.
      auto toPx = [&](float u, float v) {
        world::rotateUV(u, v, overlayRotation_);
        return ImVec2(p0.x + u * cell, p0.y + (1.0f - v) * cell);
      };
      for (const auto& t : shapes[static_cast<std::size_t>(s)])
        dl->AddTriangleFilled(toPx(t.u0, t.v0), toPx(t.u1, t.v1),
                              toPx(t.u2, t.v2), cOverlay);
      dl->AddRect(p0, p1, overlayShape_ == s ? cSel : IM_COL32(90, 90, 90, 255),
                  0.0f, 0, overlayShape_ == s ? 2.5f : 1.0f);
      ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Text("Shape %d", overlayShape_);
    ImGui::Separator();
    ImGui::TextDisabled("Rotation (Q / E)");
    ImGui::Text("%d\xC2\xB0", (overlayRotation_ & 3) * 90);
    ImGui::SameLine();
    if (ImGui::SmallButton("Q -90##ov")) overlayRotation_ = (overlayRotation_ + 3) & 3;
    ImGui::SameLine();
    if (ImGui::SmallButton("E +90##ov")) overlayRotation_ = (overlayRotation_ + 1) & 3;
    ImGui::TextDisabled("L-click: paint  R-click: erase");
  }

  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::drawPreferencesWindow() {
  if (!showPrefsWindow_) return;

  ImGui::SetNextWindowSize(ImVec2(620.0f, 480.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Preferences", &showPrefsWindow_)) { ImGui::End(); return; }

  // Category list on the left
  constexpr const char* kCategories[] = { "Water", "Lighting", "Fog", "Ambient Occlusion", "Rendering" };
  constexpr int kNumCat = static_cast<int>(std::size(kCategories));

  ImGui::BeginChild("##prefs_cats", ImVec2(140.0f, 0.0f), true);
  for (int i = 0; i < kNumCat; ++i) {
    const bool sel = (prefsCategory_ == i);
    if (ImGui::Selectable(kCategories[i], sel))
      prefsCategory_ = i;
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // Right panel — settings for selected category
  ImGui::BeginChild("##prefs_content", ImVec2(0.0f, 0.0f), false);

  switch (prefsCategory_) {
    case 0: { // Water
      drawWaterSettings();
      break;
    }
    case 1: { // Lighting
      ImGui::SeparatorText("Sun Direction");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Yaw##lgt",   &sunYawDeg_,   0.0f, 360.0f, "%.0f°");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Pitch##lgt", &sunPitchDeg_, 10.0f,  90.0f, "%.0f°");
      ImGui::SeparatorText("Intensity");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Ambient##lgt",  &ambient_, 0.0f, 1.0f, "%.2f");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Diffuse##lgt",  &diffuse_, 0.0f, 1.0f, "%.2f");
      ImGui::SeparatorText("Shadows");
      ImGui::Checkbox("Enable Shadows", &shadowsEnabled_);
      ImGui::BeginDisabled(!shadowsEnabled_);
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Half Extent##sh", &shadowHalfExtent_, 10.0f, 100.0f, "%.0f");
      ImGui::EndDisabled();
      break;
    }
    case 2: { // Fog
      ImGui::Checkbox("Enable Fog", &fogEnabled_);
      ImGui::BeginDisabled(!fogEnabled_);
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Density##fog", &fogDensity_, 0.0f, 0.1f,   "%.4f");
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Start##fog",   &fogStart_,   0.0f, 120.0f, "%.1f");
      ImGui::ColorEdit3("Color##fog", reinterpret_cast<float*>(&fogColor_));
      if (ImGui::Button("Reset Fog Defaults")) {
        fogDensity_ = 0.015f; fogStart_ = 5.0f;
        fogColor_ = {0.58f, 0.67f, 0.78f};
      }
      ImGui::EndDisabled();
      break;
    }
    case 3: { // Ambient Occlusion
      ImGui::Checkbox("Enable AO", &aoEnabled_);
      ImGui::BeginDisabled(!aoEnabled_);
      ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Strength##ao", &aoStrength_, 0.0f, 1.0f, "%.2f");
      if (ImGui::Button("Reset AO Defaults")) aoStrength_ = 0.50f;
      ImGui::Separator();
      ImGui::EndDisabled();
      if (aoEnabled_) ImGui::TextDisabled("AO is baked — rebuild terrain to update.");
      break;
    }
    case 4: { // Rendering
      ImGui::Checkbox("Lighting",             &lightingEnabled_);
      ImGui::Checkbox("Palette Quantisation", &palette_);
      if (palette_) {
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("Hues##pal",  &paletteHues_, 2, 128);
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("Sats##pal",  &paletteSats_, 2, 64);
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("Lums##pal",  &paletteLums_, 2, 96);
      }
      ImGui::Checkbox("Wireframe",            &showWireframe_);
      ImGui::Separator();
      ImGui::TextDisabled("Overlays");
      if (ImGui::Checkbox("Height Overlay",      &showHeightOverlay_))      overlayHeightAuto_      = false;
      if (ImGui::Checkbox("Walkability Overlay",  &showWalkabilityOverlay_)) overlayWalkabilityAuto_ = false;
      ImGui::Checkbox("Gridmap Overlay",          &showGridmapOverlay_);
      break;
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Save as Default")) saveSettings();
  ImGui::SameLine();
  ImGui::TextDisabled("Writes to settings.cfg");

  // DB connection status — shown at the bottom of the panel
  ImGui::Spacing();
  ImGui::Separator();
  if (dbLoaded_) {
    ImGui::TextColored({0.4f, 0.9f, 0.4f, 1.0f}, "DB: connected");
  } else {
    ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "DB: offline");
    ImGui::TextDisabled("(built-ins only)");
    if (ImGui::SmallButton("Retry")) {
      try { dbLoadAll(); } catch (...) {}
    }
  }

  ImGui::EndChild();
  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::draw3DViewportWindow() {
  ImGui::Begin(kViewport3dName, nullptr, ImGuiWindowFlags_NoScrollbar);

  // Resize FBO to content area
  {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int nw = std::max(4, static_cast<int>(avail.x));
    const int nh = std::max(4, static_cast<int>(avail.y));
    if (nw != viewport3dW_ || nh != viewport3dH_) {
      viewport3dW_ = nw; viewport3dH_ = nh;
      viewport3dFbo_->resize(nw, nh);
    }
  }

  const GLuint tex = viewport3dFbo_->resolveColorTexture();
  ImVec2 imgPos = ImGui::GetCursorScreenPos();
  ImGui::Image((ImTextureID)(uintptr_t)(tex),
               ImVec2(static_cast<float>(viewport3dW_),
                      static_cast<float>(viewport3dH_)),
               ImVec2(0, 1), ImVec2(1, 0));

  // Interaction when cursor is over the image
  const bool imageHovered = ImGui::IsItemHovered();
  const auto& io = ImGui::GetIO();

  // Track middle-click origin: if it started in this viewport, keep orbit
  // locked here until the button is released — prevents 2D grid from panning.
  if (imageHovered && io.MouseClicked[ImGuiMouseButton_Middle])
    middleClickIn3D_ = true;
  if (!io.MouseDown[ImGuiMouseButton_Middle])
    middleClickIn3D_ = false;

  // Ctrl+Scroll → brush size; plain scroll → camera zoom
  if (imageHovered && io.MouseWheel != 0.0f) {
    if (io.KeyCtrl) {
      brush_.size = std::clamp(brush_.size + (io.MouseWheel > 0 ? 1 : -1), 1, 64);
    } else {
      camera_.onScroll(static_cast<double>(io.MouseWheel));
    }
  }

  if (imageHovered) {
    const float px = io.MousePos.x - imgPos.x;
    const float py = io.MousePos.y - imgPos.y;
    if (px >= 0 && py >= 0 && px < viewport3dW_ && py < viewport3dH_) {
      const float aspect = (viewport3dH_ > 0)
        ? static_cast<float>(viewport3dW_) / static_cast<float>(viewport3dH_) : 1.0f;
      const glm::mat4 vp = camera_.viewProjection(aspect);
      glm::vec3 ro, rd;
      input::screenToRay(px, py, viewport3dW_, viewport3dH_, vp, &ro, &rd);

      const auto pick = input::pickTile(ro, rd, map_.vertexHeights, map_.width, map_.height);
      if (pick.hit) {
        hoveredTileX_ = pick.tileX;
        hoveredTileY_ = pick.tileY;
      }

      // Left-click apply tool; right-click applies secondary action for
      // Terrain (lower) and Blocking (unblock).
      const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
      // Right-click is a valid secondary action for every tool except PaintTerrain
      // (which has no secondary action) and PlaceSpawn (single-click only).
      const bool rmbActive = rmbDown && pick.hit &&
                             activeTool_ != EditorTool::PaintTerrain &&
                             activeTool_ != EditorTool::PlaceSpawn;

      if (pick.hit && (lmbDown || rmbActive)) {
        if (!mouseHeld3D_) {
          mouseHeld3D_ = true;
          if (!undoPending_) pushUndo();
          undoPending_ = true;
        }
        applyBrush(pick.tileX, pick.tileY, io.DeltaTime, rmbDown && !lmbDown);
        hadStroke_ = true;
      } else if (!lmbDown && !rmbActive) {
        mouseHeld3D_ = false;
      }
    }
  } else {
    mouseHeld3D_ = false;
  }

  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::drawGridView() {
  ImGui::Begin(kGridName);

  const ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
  const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  ImDrawList* dl           = ImGui::GetWindowDrawList();
  const auto& io           = ImGui::GetIO();

  if (ImGui::IsWindowHovered()) {
    // Ctrl+Scroll → brush size
    if (io.MouseWheel != 0.0f) {
      if (io.KeyCtrl) {
        brush_.size = std::clamp(brush_.size + (io.MouseWheel > 0 ? 1 : -1), 1, 64);
      } else {
        gridZoom_ = std::clamp(gridZoom_ * (io.MouseWheel > 0 ? 1.15f : (1.0f / 1.15f)), 2.0f, 32.0f);
      }
    }
    if (!middleClickIn3D_ && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
      gridOffX_ += delta.x;
      gridOffY_ += delta.y;
    }
  }

  const float z = gridZoom_;
  const int   W = map_.width;
  const int   H = map_.height;

  // Horizontal flip: +tileX (east) is drawn on the LEFT, matching the 3D
  // viewport's lookAtLH convention (east = screen-left) and the in-game
  // minimap. sx() maps a tile-space X (can be fractional, e.g. tile edges)
  // to screen X with the mirror applied; sy() is the unflipped Y mapping.
  const float ox = canvasPos.x + gridOffX_;
  const float oy = canvasPos.y + gridOffY_;
  auto sx       = [&](float txf) { return ox + (static_cast<float>(W) - txf) * z; };
  auto sy       = [&](float tyf) { return oy + tyf * z; };
  auto colLeftX = [&](int tx)    { return sx(static_cast<float>(tx) + 1.0f); }; // rect left edge

  // O(1) per-tile overlay lookup, built once per frame. (Scanning the whole
  // overlayTiles vector for every visible tile was O(tiles × overlays) — a hard
  // hang on large, heavily-painted maps.)
  std::unordered_map<int, const shared::OverlayTile*> ovByTile;
  ovByTile.reserve(map_.overlayTiles.size() * 2);
  for (const auto& ov : map_.overlayTiles)
    ovByTile[ov.tileY * W + ov.tileX] = &ov;

  // Visible-X cull range in flipped space (u = W-1-tx is the unflipped index).
  const int u0 = std::max(0, static_cast<int>((-gridOffX_) / z));
  const int u1 = std::min(W, static_cast<int>((-gridOffX_ + canvasSize.x) / z) + 2);
  const int x0 = std::max(0, W - u1);
  const int x1 = std::min(W, W - u0);
  const int y0 = std::max(0, static_cast<int>((-gridOffY_) / z));
  const int y1 = std::min(H, static_cast<int>((-gridOffY_ + canvasSize.y) / z) + 2);

  for (int ty = y0; ty < y1; ++ty) {
    if (ty >= static_cast<int>(map_.tiles.size())) break;
    for (int tx = x0; tx < x1; ++tx) {
      if (tx >= static_cast<int>(map_.tiles[ty].size())) break;
      const auto& tile = map_.tiles[ty][tx];

      float fr = 0.29f, fg = 0.49f, fb = 0.16f;
      hexToRgbf(tile.groundColor.c_str(), fr, fg, fb);

      if (showHeightOverlay_) {
        const int vW = W + 1;
        const auto& vh = map_.vertexHeights;
        float h = 0.0f;
        if (!vh.empty() && ty < H && tx < W) {
          h += vh[static_cast<std::size_t>((H - ty)     * vW + tx)];
          h += vh[static_cast<std::size_t>((H - ty)     * vW + tx + 1)];
          h += vh[static_cast<std::size_t>((H - ty - 1) * vW + tx)];
          h += vh[static_cast<std::size_t>((H - ty - 1) * vW + tx + 1)];
          h *= 0.25f;
        }
        const float g = std::clamp(h, 0.0f, 1.0f);
        fr = fr * 0.4f + g * 0.6f;
        fg = fg * 0.4f + g * 0.6f;
        fb = fb * 0.4f + g * 0.6f;
      }

      const float px = colLeftX(tx);
      const float py = sy(static_cast<float>(ty));
      dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z),
        IM_COL32(static_cast<int>(fr * 255), static_cast<int>(fg * 255),
                  static_cast<int>(fb * 255), 255));

      // Overlay shape drawn over the tile in the material colour. (u,v) maps to
      // the tile rect the same way the minimap rasteriser does, so the 2D grid,
      // minimap, and 3D view agree on shape orientation.
      if (auto it = ovByTile.find(ty * W + tx); it != ovByTile.end()) {
        const auto& ov = *it->second;
        const auto& mats = world::overlayMaterials();
        if (ov.materialId > 0 && ov.materialId < static_cast<int>(mats.size())) {
          const auto& m = mats[static_cast<std::size_t>(ov.materialId)];
          const ImU32 col = IM_COL32(m.mr, m.mg, m.mb, 255);
          auto toPx = [&](float u, float v) {
            world::rotateUV(u, v, ov.rotation);
            return ImVec2(px + u * z, py + (1.0f - v) * z);
          };
          for (const auto& t : world::overlayShapeTriangles()[static_cast<std::size_t>(ov.shape)])
            dl->AddTriangleFilled(toPx(t.u0, t.v0), toPx(t.u1, t.v1), toPx(t.u2, t.v2), col);
        }
      }

      // Walkability overlay
      if (showWalkabilityOverlay_ && !tile.walkable) {
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z), IM_COL32(220, 30, 30, 110));
        // Draw X for non-walkable tiles if zoomed in enough
        if (z >= 6.0f) {
          const float m = z * 0.15f;
          dl->AddLine(ImVec2(px + m, py + m), ImVec2(px + z - m, py + z - m), IM_COL32(255, 60, 60, 200), 1.5f);
          dl->AddLine(ImVec2(px + z - m, py + m), ImVec2(px + m, py + z - m), IM_COL32(255, 60, 60, 200), 1.5f);
        }
      }
      if (showGridmapOverlay_) {
        const ImU32 col = tile.walkable ? IM_COL32(0, 200, 0, 60) : IM_COL32(200, 0, 0, 60);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z), col);
      }

      // Obstacle dot
      if (z >= 6.0f && !tile.obstacle.empty() && tile.obstacle != "none") {
        ImU32 oc = IM_COL32(20, 90, 10, 255);
        if (tile.obstacle == "rock")  oc = IM_COL32(110, 110, 110, 255);
        if (tile.obstacle == "chest") oc = IM_COL32(200, 160, 30,  255);
        if (tile.obstacle == "fence") oc = IM_COL32(100, 60,  20,  255);
        dl->AddCircleFilled(ImVec2(px + z * 0.5f, py + z * 0.5f), std::max(2.0f, z * 0.28f), oc);
      }

      if (z >= 6.0f)
        dl->AddRect(ImVec2(px, py), ImVec2(px + z, py + z), IM_COL32(0, 0, 0, 40));
    }
  }

  // ---- Neighbor-chunk border tiles (read-only ghosts) ---------------------
  // When the open map is assigned to a world cell, draw a strip of each
  // neighbor's tiles beyond the [0,W)/[0,H) range at reduced alpha so terrain,
  // paths, and walls can be authored to line up across the seam. sx()/sy()
  // accept out-of-range tile-space coords, so this is just an offset lookup.
  for (const auto& np : neighbors_) {
    const int nW = np.map.width, nH = np.map.height;
    const int strip = neighborStripTiles_;
    const int nx0 = (np.dcx > 0) ? 0 : (np.dcx < 0 ? std::max(0, nW - strip) : 0);
    const int nx1 = (np.dcx > 0) ? std::min(nW, strip) : nW;
    const int ny0 = (np.dcy > 0) ? 0 : (np.dcy < 0 ? std::max(0, nH - strip) : 0);
    const int ny1 = (np.dcy > 0) ? std::min(nH, strip) : nH;
    for (int ny = ny0; ny < ny1; ++ny) {
      if (ny >= static_cast<int>(np.map.tiles.size())) break;
      for (int nx = nx0; nx < nx1; ++nx) {
        if (nx >= static_cast<int>(np.map.tiles[ny].size())) break;
        const float txf = static_cast<float>(nx + np.dcx * nW);
        const float tyf = static_cast<float>(ny + np.dcy * nH);
        const float px = sx(txf + 1.0f);
        const float py = sy(tyf);
        if (px + z < canvasPos.x || px > canvasPos.x + canvasSize.x ||
            py + z < canvasPos.y || py > canvasPos.y + canvasSize.y) continue;
        float fr = 0.29f, fg = 0.49f, fb = 0.16f;
        hexToRgbf(np.map.tiles[ny][nx].groundColor.c_str(), fr, fg, fb);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z),
          IM_COL32(static_cast<int>(fr * 140), static_cast<int>(fg * 140),
                   static_cast<int>(fb * 140), 255));
      }
    }
  }

  // ---- Walls + pillars (white edge/corner lines, OSRS-style) --------------
  // Drawn in tile space; sx()/sy() apply the horizontal flip. Orient: 0=+Z
  // (south/bottom), 2=+X (east), 4=-Z (north/top), 6=-X (west); odd = diagonal.
  {
    const ImU32 wc = IM_COL32(255, 255, 255, 235);
    const float th = std::max(1.0f, z * 0.12f);
    for (const auto& w : map_.walls) {
      if (w.tileX < 0 || w.tileY < 0 || w.tileX >= W || w.tileY >= H) continue;
      const float tx = static_cast<float>(w.tileX);
      const float ty = static_cast<float>(w.tileY);
      const float L = sx(tx),      R = sx(tx + 1.0f);   // L = east edge (flipped → larger px)
      const float T = sy(ty),      B = sy(ty + 1.0f);
      const int   o = w.orient & 7;
      if (w.pillar) {
        const float cx = (o == 0 || o == 2) ? sx(tx + 1.0f) : sx(tx); // +X corner = east
        const float cy = (o == 0 || o == 6) ? B : T;                  // +Z corner = bottom
        dl->AddCircleFilled(ImVec2(cx, cy), std::max(1.5f, z * 0.18f), wc);
      } else if ((o & 1) == 0) {
        if      (o == 0) dl->AddLine(ImVec2(L, B), ImVec2(R, B), wc, th); // +Z bottom
        else if (o == 2) dl->AddLine(ImVec2(R, T), ImVec2(R, B), wc, th); // +X east edge
        else if (o == 4) dl->AddLine(ImVec2(L, T), ImVec2(R, T), wc, th); // -Z top
        else             dl->AddLine(ImVec2(L, T), ImVec2(L, B), wc, th); // -X west edge
      } else {
        if (o == 1 || o == 5) dl->AddLine(ImVec2(R, T), ImVec2(L, B), wc, th); // (east,north)-(west,south)
        else                  dl->AddLine(ImVec2(L, T), ImVec2(R, B), wc, th); // (west,north)-(east,south)
      }
    }
  }

  // NPC markers
  for (const auto& n : npcSpawns_) {
    const float px = colLeftX(n.tileX) + z * 0.5f;
    const float py = sy(static_cast<float>(n.tileY)) + z * 0.5f;
    const ImU32 nc = (n.kind == "shopkeeper") ? IM_COL32(180, 50, 220, 255) : IM_COL32(255, 220, 0, 255);
    dl->AddCircleFilled(ImVec2(px, py), std::max(2.0f, z * 0.25f), nc);
  }

  // Spawn cross
  {
    const float px = colLeftX(map_.spawnPoint[0]) + z * 0.5f;
    const float py = sy(static_cast<float>(map_.spawnPoint[1])) + z * 0.5f;
    const float arm = std::max(3.0f, z * 0.4f);
    dl->AddLine(ImVec2(px - arm, py), ImVec2(px + arm, py), IM_COL32(255, 255, 255, 230), 2.0f);
    dl->AddLine(ImVec2(px, py - arm), ImVec2(px, py + arm), IM_COL32(255, 255, 255, 230), 2.0f);
  }

  // Brush preview (round or square)
  if (hoveredTileX_ >= 0 && z >= 2.0f) {
    const int half = brush_.size / 2;
    if (brush_.shape == BrushShape::Square) {
      // Flipped: leftmost screen tile is the highest tile index in the span.
      const float px = colLeftX(hoveredTileX_ + half);
      const float py = sy(static_cast<float>(hoveredTileY_ - half));
      const float s  = brush_.size * z;
      dl->AddRect(ImVec2(px, py), ImVec2(px + s, py + s), IM_COL32(255, 220, 30, 200), 0.0f, 0, 1.5f);
    } else {
      // Round: draw each tile in the round mask as a small highlighted rect
      const float r = static_cast<float>(half);
      for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx) {
          const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
          if (d > r + 0.5f) continue;
          const int tx = hoveredTileX_ + dx;
          const int ty = hoveredTileY_ + dy;
          if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
          const float px = colLeftX(tx);
          const float py = sy(static_cast<float>(ty));
          dl->AddRect(ImVec2(px, py), ImVec2(px + z, py + z), IM_COL32(255, 220, 30, 160), 0.0f, 0, 1.5f);
        }
      }
    }
  }

  // Invisible button to capture mouse
  ImGui::SetCursorScreenPos(canvasPos);
  ImGui::InvisibleButton("##gridcanvas", canvasSize,
                         ImGuiButtonFlags_MouseButtonLeft |
                         ImGuiButtonFlags_MouseButtonRight);

  if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    const float mpx = io.MousePos.x;
    const float mpy = io.MousePos.y;
    // Inverse of the horizontal flip: screen-left = highest tile index.
    const int tx = (W - 1) - static_cast<int>((mpx - ox) / z);
    const int ty = static_cast<int>((mpy - oy) / z);

    if (tx >= 0 && tx < W && ty >= 0 && ty < H) {
      hoveredTileX_ = tx;
      hoveredTileY_ = ty;

      const bool lmbGrid = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      const bool rmbGrid = ImGui::IsMouseDown(ImGuiMouseButton_Right);
      // Right-click routes through applyBrush for all tools that handle it.
      const bool rmbSecondary = rmbGrid &&
                                activeTool_ != EditorTool::PaintTerrain &&
                                activeTool_ != EditorTool::PlaceSpawn;

      if (lmbGrid || rmbSecondary) {
        if (!mouseHeldGrid_) {
          mouseHeldGrid_ = true;
          if (!undoPending_) pushUndo();
          undoPending_ = true;
        }
        applyBrush(tx, ty, 0.016f, rmbGrid && !lmbGrid);
        hadStroke_ = true;
      } else if (rmbGrid) {
        // Right-click with other tools: erase
        if (!mouseHeldGrid_) {
          mouseHeldGrid_ = true;
          if (!undoPending_) pushUndo();
          undoPending_ = true;
        }
        if (ty < static_cast<int>(map_.tiles.size()) &&
            tx < static_cast<int>(map_.tiles[ty].size())) {
          setObstacleAtTile(tx, ty, "");
          map_.tiles[ty][tx].walkable = true;
          npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
            [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
            npcSpawns_.end());
          auto& ov = map_.overlayTiles;
          ov.erase(std::remove_if(ov.begin(), ov.end(),
            [tx, ty](const shared::OverlayTile& o){ return o.tileX == tx && o.tileY == ty; }),
            ov.end());
          rebuildObstacles();
          waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
          minimap_.rebuild(map_, npcSpawns_);
        }
        hadStroke_ = true;
      } else {
        mouseHeldGrid_ = false;
      }
    } else {
      mouseHeldGrid_ = false;
    }
  } else {
    mouseHeldGrid_ = false;
  }

  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::drawMinimapWindow() {
  ImGui::SetNextWindowSize(ImVec2(196.0f, 220.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin(kMinimapName);
  const GLuint mmTex = minimap_.texture();
  if (mmTex) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  sz    = std::min(avail.x, avail.y);
    // Mirror horizontally (u0=1, u1=0) so +tileX (east) appears on the LEFT,
    // matching the 3D viewport's lookAtLH convention (east = screen-left).
    ImGui::Image((ImTextureID)(uintptr_t)(mmTex), ImVec2(sz, sz), ImVec2(1, 0), ImVec2(0, 1));
  } else {
    ImGui::TextDisabled("(no minimap)");
  }
  ImGui::End();
}

// -----------------------------------------------------------------------
// Brush: collect dirty flags, do ONE rebuild after all tiles processed.
void EditorApp::applyBrush(int cx, int cy, float dt, bool rightClick) {
  // Flatten is an area operation (needs the whole brush's average), so it's
  // handled here rather than per-tile in applyToolAt.
  if (activeTool_ == EditorTool::FlattenTerrain) { applyFlatten(cx, cy); return; }

  const int half = brush_.size / 2;
  const float r  = static_cast<float>(half);

  bool dirtyTerrain   = false;
  bool dirtyObstacles = false;
  bool dirtyMinimap   = false;
  bool dirtyWater     = false;

  for (int dy = -half; dy <= half; ++dy) {
    for (int dx = -half; dx <= half; ++dx) {
      if (brush_.shape == BrushShape::Round) {
        const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        if (d > r + 0.5f) continue;
      }
      const int tx = cx + dx, ty = cy + dy;
      applyToolAt(tx, ty, dt, rightClick, dirtyTerrain, dirtyObstacles, dirtyMinimap, dirtyWater);
    }
  }

  if (dirtyTerrain)   rebuildTerrainGL();
  if (dirtyWater)     waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
  if (dirtyObstacles) rebuildObstacles();
  if (dirtyMinimap)   minimap_.rebuild(map_, npcSpawns_);
}

// -----------------------------------------------------------------------
// Flatten: pull every vertex under the brush toward the brush's average
// height. Strength controls how hard (a quick pass smooths bumps; holding it
// makes a dead-flat pad at the area's existing elevation).
void EditorApp::applyFlatten(int cx, int cy) {
  const int   W = map_.width, H = map_.height;
  auto&       vh = map_.vertexHeights;
  if (vh.empty()) return;

  const int   half = brush_.size / 2;
  const float r    = static_cast<float>(half);

  // Collect the unique vertices belonging to the brush tiles.
  std::unordered_set<int> verts;
  for (int dy = -half; dy <= half; ++dy) {
    for (int dx = -half; dx <= half; ++dx) {
      if (brush_.shape == BrushShape::Round &&
          std::sqrt(static_cast<float>(dx * dx + dy * dy)) > r + 0.5f) continue;
      const int tx = cx + dx, ty = cy + dy;
      if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
      for (int vrow = H - ty - 1; vrow <= H - ty; ++vrow)
        for (int vcol = tx; vcol <= tx + 1; ++vcol) {
          if (vrow < 0 || vrow > H || vcol < 0 || vcol > W) continue;
          verts.insert(vrow * (W + 1) + vcol);
        }
    }
  }
  if (verts.empty()) return;

  double sum = 0.0;
  for (int i : verts) sum += vh[i];
  const float avg = static_cast<float>(sum / verts.size());

  // Pull factor per stroke; scaled so a high strength flattens almost instantly.
  const float t = std::clamp(brush_.strength * 4.0f, 0.0f, 1.0f);
  for (int i : verts) vh[i] = std::clamp(vh[i] + (avg - vh[i]) * t, 0.0f, 1.0f);

  rebuildTerrainGL();
  rebuildObstacles();   // objects / walls follow terrain height
}

// -----------------------------------------------------------------------
void EditorApp::applyToolAt(int tx, int ty, float dt, bool rightClick,
                             bool& dirtyTerrain, bool& dirtyObstacles,
                             bool& dirtyMinimap,  bool& dirtyWater) {
  if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) return;
  if (ty >= static_cast<int>(map_.tiles.size()))     return;
  if (tx >= static_cast<int>(map_.tiles[ty].size())) return;

  auto& tile = map_.tiles[ty][tx];

  switch (activeTool_) {
    case EditorTool::PaintTerrain: {
      tile.groundColor = rgbfToHex(paletteR_, paletteG_, paletteB_);
      dirtyTerrain = true;
      dirtyMinimap = true;
      break;
    }
    case EditorTool::SculptTerrain: {
      // Left-click = raise, right-click = lower
      const float dir = rightClick ? -1.0f : 1.0f;
      const int W = map_.width, H = map_.height;
      auto& vh = map_.vertexHeights;
      if (vh.empty()) break;

      const float halfF = static_cast<float>(brush_.size) * 0.5f;
      auto gaussW = [&](int vcol, int vrow_world) -> float {
        const float dx = vcol - (tx + 0.5f);
        const float dy = vrow_world - (ty + 0.5f);
        const float d  = std::sqrt(dx * dx + dy * dy);
        const float sig = halfF + 0.5f;
        return std::exp(-(d * d) / (2.0f * sig * sig));
      };

      for (int vrow = H - ty - 1; vrow <= H - ty; ++vrow) {
        for (int vcol = tx; vcol <= tx + 1; ++vcol) {
          if (vrow < 0 || vrow > H || vcol < 0 || vcol > W) continue;
          const std::size_t idx = static_cast<std::size_t>(vrow * (W + 1) + vcol);
          const float w = gaussW(vcol, H - vrow);
          vh[idx] = std::clamp(vh[idx] + dir * brush_.strength * w * dt, 0.0f, 1.0f);
        }
      }
      dirtyTerrain   = true;
      dirtyObstacles = true;   // obstacle/NPC positions follow terrain height
      break;
    }
    case EditorTool::PlaceObstacle: {
      if (rightClick) {
        setObstacleAtTile(tx, ty, "");
        tile.walkable = true;
      } else {
        setObstacleAtTile(tx, ty, obstacleSubtype_);
      }
      dirtyObstacles = true;
      dirtyMinimap   = true;
      break;
    }
    case EditorTool::PlaceWall:
    case EditorTool::PlacePillar: {
      const bool pillar  = (activeTool_ == EditorTool::PlacePillar);
      const int  orient  = pillar ? pillarOrient_ : wallOrient_;
      const std::string& objId = pillar ? pillarSubtype_ : wallSubtype_;
      auto& ws = map_.walls;
      // A tile may hold several walls (one per edge — e.g. N+E make a corner)
      // and pillars, deduped per orientation. Left-click adds the current
      // orient; right-click removes the wall/pillar at that exact orient.
      auto matches = [&](const shared::WallSeg& w){
        return w.tileX == tx && w.tileY == ty &&
               w.pillar == pillar && w.orient == orient; };
      if (rightClick) {
        ws.erase(std::remove_if(ws.begin(), ws.end(), matches), ws.end());
      } else if (!std::any_of(ws.begin(), ws.end(), matches)) {
        ws.push_back({ tx, ty, orient, pillar, objId });
      }
      dirtyObstacles = true;   // walls rebuild alongside obstacles
      dirtyMinimap   = true;
      break;
    }
    case EditorTool::PlaceNPC: {
      if (rightClick) {
        npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
          [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
          npcSpawns_.end());
      } else {
        npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
          [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
          npcSpawns_.end());
        shared::NpcSpawn ns; ns.kind = npcSubtype_; ns.tileX = tx; ns.tileY = ty;
        npcSpawns_.push_back(ns);
      }
      dirtyMinimap = true;
      break;
    }
    case EditorTool::PlaceSpawn: {
      map_.spawnPoint = { tx, ty };
      dirtyMinimap = true;
      break;
    }
    case EditorTool::PaintBlocking: {
      // Left-click = block (non-walkable), right-click = unblock (walkable)
      tile.walkable = rightClick;
      break;
    }
    case EditorTool::PaintOverlay: {
      // One overlay per tile: remove any existing overlay here first.
      auto& ov = map_.overlayTiles;
      const bool hadWater = overlayIsWater(map_, tx, ty);
      ov.erase(std::remove_if(ov.begin(), ov.end(),
          [tx, ty](const shared::OverlayTile& o){
            return o.tileX == tx && o.tileY == ty; }),
          ov.end());
      if (!rightClick && overlayMaterial_ > 0) {
        ov.push_back(shared::OverlayTile{ tx, ty, overlayShape_, overlayMaterial_,
                                          overlayRotation_ });
        // Water material auto-blocks the tile, but drapes flush on the terrain
        // (no carving / no terrain modification). Obstacles left untouched.
        if (overlayMaterial_ == shared::kWaterMaterialId)
          tile.walkable = false;
      } else if (rightClick && hadWater) {
        // Erased a water overlay — restore walkability.
        tile.walkable = true;
      }
      dirtyWater   = true;   // overlay + water share the rebuild path
      dirtyMinimap = true;
      break;
    }
    case EditorTool::Erase: {
      setObstacleAtTile(tx, ty, "");
      tile.walkable = true;
      npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
        [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
        npcSpawns_.end());
      {
        auto& ov = map_.overlayTiles;
        ov.erase(std::remove_if(ov.begin(), ov.end(),
            [tx, ty](const shared::OverlayTile& o){
              return o.tileX == tx && o.tileY == ty; }),
            ov.end());
      }
      dirtyObstacles = true;
      dirtyMinimap   = true;
      dirtyWater     = true;
      break;
    }
  }
}

// -----------------------------------------------------------------------
void EditorApp::repaintVertexColors(int, int, int, int) { rebuildTerrainGL(); }
void EditorApp::resculptNormals(int, int, int, int)     { rebuildTerrainGL(); }

// -----------------------------------------------------------------------
// Blocked-tile overlay VBO (3D red X marks)
void EditorApp::initBlockedOverlay() {
  glCreateVertexArrays(1, &blockedVao_);
  glCreateBuffers(1, &blockedVbo_);
  // Pre-allocate for 64×64 × 4 vertices (2 lines × 2 pts each) × 3 floats
  const std::size_t cap = static_cast<std::size_t>(256 * 256 * 4 * 3) * sizeof(float);
  glNamedBufferStorage(blockedVbo_, static_cast<GLsizeiptr>(cap), nullptr, GL_DYNAMIC_STORAGE_BIT);
  glVertexArrayVertexBuffer(blockedVao_, 0, blockedVbo_, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(blockedVao_, 0);
  glVertexArrayAttribFormat(blockedVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(blockedVao_, 0, 0);
  blockedGLInited_ = true;
}

void EditorApp::rebuildBlockedOverlay() {
  if (!blockedGLInited_) return;
  std::vector<float> verts;
  verts.reserve(static_cast<std::size_t>(map_.width * map_.height) * 4 * 3);

  for (int ty = 0; ty < map_.height; ++ty) {
    if (ty >= static_cast<int>(map_.tiles.size())) break;
    for (int tx = 0; tx < map_.width; ++tx) {
      if (tx >= static_cast<int>(map_.tiles[ty].size())) break;
      if (map_.tiles[ty][tx].walkable) continue;

      const float x  = static_cast<float>(tx);
      const float z  = static_cast<float>(ty);
      const float y  = tileWorldY(tx, ty) + 0.12f;
      const float m  = 0.38f;  // half-width of X arm

      // Line 1: SW→NE
      verts.insert(verts.end(), { x - m, y, z - m,  x + m, y, z + m });
      // Line 2: NW→SE
      verts.insert(verts.end(), { x + m, y, z - m,  x - m, y, z + m });
    }
  }

  blockedLineCount_ = static_cast<int>(verts.size() / 3);
  if (blockedLineCount_ > 0) {
    glNamedBufferSubData(blockedVbo_, 0,
                         static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                         verts.data());
  }
}

// -----------------------------------------------------------------------
void EditorApp::initNewMap(int w, int h) {
  // A brand-new map has no place in the world yet, so it must not inherit the
  // previously-open map's neighbor ghosts. Clearing the path first makes the
  // worldRefreshNeighbors() call below a no-op (no manifest cell matches).
  currentFilePath_.clear();
  map_ = {};
  map_.width  = w;
  map_.height = h;
  map_.spawnPoint = { w / 2, h / 2 };
  map_.tiles.assign(static_cast<std::size_t>(h),
                    std::vector<shared::TileData>(static_cast<std::size_t>(w)));
  for (int ty = 0; ty < h; ++ty) {
    for (int tx = 0; tx < w; ++tx) {
      auto& t = map_.tiles[ty][tx];
      t.x = tx; t.y = ty; t.walkable = true;
      t.groundColor = kDefaultGroundColor;
      t.type = shared::TileType::grass;
      t.obstacle = "";
      t.blocksRanged = false; t.height = 0.0f;
    }
  }
  map_.vertexHeights.assign(static_cast<std::size_t>((w + 1) * (h + 1)), 0.0f);
  map_.waterTiles.clear();
  map_.overlayTiles.clear();

  rebuildTerrainGL();
  rebuildObstacles();
  waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
  npcSpawns_.clear();
  undo_.clear();
  pushUndo();
  minimap_.init(w, h);
  minimap_.rebuild(map_, npcSpawns_);
  camera_.snapTo({ static_cast<float>(w) * 0.5f, 0.0f, static_cast<float>(h) * 0.5f });
  worldRefreshNeighbors();   // unassigned map → clears any lingering ghosts
}

void EditorApp::rebuildTerrainGL() {
  terrainData_ = world::buildTerrainMesh(map_);
  terrainMesh_.upload(terrainData_.positions, terrainData_.colors,
                      terrainData_.triangleIndices, terrainData_.lineIndices,
                      terrainData_.normals);
}

void EditorApp::rebuildObstacles() {
  obstacles_.rebuildFromMap(map_);
  walls_.rebuildFromMap(map_);
}

// -----------------------------------------------------------------------
void EditorApp::setObstacleAtTile(int tx, int ty, const std::string& obs) {
  const int W = map_.width, H = map_.height;
  if (ty < 0 || ty >= H || tx < 0 || tx >= W) return;
  if (ty >= static_cast<int>(map_.tiles.size())) return;
  auto& anchor = map_.tiles[ty][tx];

  // Footprint + collision come from the definition. When clearing, use whatever
  // is currently on the tile so the full NxM block is restored.
  const std::string lookupId = obs.empty() ? anchor.obstacle : obs;
  int sx = 1, sy = 1;
  std::string collision = "full_blocking";
  if (const auto* def = obstacles_.getDefinition(lookupId)) {
    sx = std::max(1, def->sizeX);
    sy = std::max(1, def->sizeY);
    collision = def->collision;
  } else if (lookupId == "fence") {
    collision = "half_blocking";
  }

  // Resolve walkability for the whole footprint.
  bool walkable = true, blocksRanged = false;
  if (!obs.empty() && obs != "none") {
    if      (collision == "none")          { walkable = true;  blocksRanged = false; }
    else if (collision == "half_blocking") { walkable = false; blocksRanged = false; }
    else                                    { walkable = false; blocksRanged = true; }
  }

  auto isWater = [&](int x, int y) { return overlayIsWater(map_, x, y); };

  // The obstacle marker lives only on the anchor tile (one rendered instance).
  anchor.obstacle = obs;
  // Stamp the current placement rotation (Q/E) onto the anchor; reset on clear.
  anchor.obstacleRotation = obs.empty() ? 0 : (placeRotation_ & 3);

  // Apply walkability across the footprint block (anchor + covered tiles).
  for (int dy = 0; dy < sy; ++dy) {
    for (int dx = 0; dx < sx; ++dx) {
      const int cx = tx + dx, cy = ty + dy;
      if (cx < 0 || cx >= W || cy < 0 || cy >= H) continue;
      auto& t = map_.tiles[cy][cx];
      const bool isAnchor = (cx == tx && cy == ty);
      // When placing, don't stomp another object's anchor on a covered tile.
      if (!isAnchor && !obs.empty() && !t.obstacle.empty()) continue;
      // When clearing, never re-open a water tile.
      if (obs.empty() && isWater(cx, cy)) continue;
      t.walkable     = walkable;
      t.blocksRanged = blocksRanged;
    }
  }
}

// -----------------------------------------------------------------------
void EditorApp::bakeWaterBank(int tx, int ty) {
  // Strategy: sample the *non-water* neighbour tiles to establish the bank
  // height (= natural terrain level), then SET the 4 corner vertices of this
  // water tile to (bankH - trenchDepth), clamped to [0,1].
  //
  // Using non-water neighbours for bankH means:
  //  - On flat terrain (all heights 0): bankH=0, carved=0. No visible trench
  //    below ground but water still covers the tile (mesh Y sits at 0.01+).
  //  - On any terrain with positive height: a proper trench is dug, with the
  //    bank edges naturally sloping into the water via shared vertices.
  //  - Connected water tiles: each tile reads from its own non-water
  //    neighbours, so there is no feedback-loop between adjacent tiles.

  const int W = map_.width, H = map_.height;
  auto& vh = map_.vertexHeights;
  if (vh.empty() || W <= 0 || H <= 0) return;
  if (tx < 0 || ty < 0 || tx >= W || ty >= H) return;

  // Build a quick is-water lookup for this call.
  auto isWater = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= W || y >= H) return false;
    return overlayIsWater(map_, x, y);
  };

  // Sample bank height = average of non-water neighbour tile centers.
  float bankSum = 0.0f;
  int   bankCnt = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      const int nx = tx + dx, ny = ty + dy;
      if (!isWater(nx, ny) && nx >= 0 && ny >= 0 && nx < W && ny < H) {
        bankSum += tileWorldY(nx, ny);
        ++bankCnt;
      }
    }
  }
  // Fallback: use the current tile's own height as bank reference.
  const float bankH = bankCnt > 0 ? bankSum / static_cast<float>(bankCnt)
                                   : tileWorldY(tx, ty);

  // Trench depth = 2× the waterOffset so water surface (bankH - offset) sits
  // visibly above the carved floor (bankH - 2*offset).
  const float trenchDepth = waterUniforms_.waterOffset * 2.0f;
  const float carvedNorm  = std::clamp((bankH - trenchDepth) / shared::kMaxTerrainH,
                                        0.0f, 1.0f);

  // The 4 corner vertices of tile (tx, ty):
  //   SW: (vc=tx,   vr=H-ty)
  //   SE: (vc=tx+1, vr=H-ty)
  //   NW: (vc=tx,   vr=H-ty-1)
  //   NE: (vc=tx+1, vr=H-ty-1)
  const int cVc[4] = { tx,   tx+1, tx,   tx+1 };
  const int cVr[4] = { H-ty, H-ty, H-ty-1, H-ty-1 };

  for (int i = 0; i < 4; ++i) {
    const int vc = cVc[i], vr = cVr[i];
    if (vc < 0 || vc > W || vr < 0 || vr > H) continue;
    const std::size_t idx = static_cast<std::size_t>(vr * (W + 1) + vc);
    vh[idx] = carvedNorm;   // absolute SET – always carve to this depth
  }
}

float EditorApp::tileWorldY(int tx, int ty) const {
  const int W = map_.width, H = map_.height;
  if (W <= 0 || H <= 0 || tx < 0 || ty < 0 || tx >= W || ty >= H) return 0.0f;
  const auto& vh = map_.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return 0.0f;
  const float SW = vh[static_cast<std::size_t>((H - ty)     * (W + 1) + tx)]     * shared::kMaxTerrainH;
  const float SE = vh[static_cast<std::size_t>((H - ty)     * (W + 1) + tx + 1)] * shared::kMaxTerrainH;
  const float NW = vh[static_cast<std::size_t>((H - ty - 1) * (W + 1) + tx)]     * shared::kMaxTerrainH;
  const float NE = vh[static_cast<std::size_t>((H - ty - 1) * (W + 1) + tx + 1)] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}

int EditorApp::clampTile(int v, int max) const { return std::clamp(v, 0, max - 1); }

// -----------------------------------------------------------------------
void EditorApp::initHoverMesh() {
  destroyHoverMesh();
  glCreateVertexArrays(1, &hoverVao_);
  glCreateBuffers(1, &hoverVbo_);
  // Large enough for a 64×64 round brush: π×32² ≈ 3217 tiles × 8 verts × 3 floats
  constexpr GLsizeiptr kHoverBufBytes = static_cast<GLsizeiptr>(3300 * 8 * 3 * sizeof(float));
  glNamedBufferStorage(hoverVbo_, kHoverBufBytes, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glVertexArrayVertexBuffer(hoverVao_, 0, hoverVbo_, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(hoverVao_, 0);
  glVertexArrayAttribFormat(hoverVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(hoverVao_, 0, 0);
}

void EditorApp::destroyHoverMesh() {
  if (hoverVbo_) { glDeleteBuffers(1, &hoverVbo_); hoverVbo_ = 0; }
  if (hoverVao_) { glDeleteVertexArrays(1, &hoverVao_); hoverVao_ = 0; }
}

void EditorApp::updateHoverMesh(int cx, int cy, int szX, int szY) {
  const int W = map_.width, H = map_.height;
  const auto& vh = map_.vertexHeights;
  if (W <= 0 || H <= 0 || vh.empty()) return;

  auto safeVH = [&](int row, int col) -> float {
    row = std::clamp(row, 0, H);
    col = std::clamp(col, 0, W);
    return vh[static_cast<std::size_t>(row * (W + 1) + col)] * shared::kMaxTerrainH;
  };

  // Inline helper: add a GL_LINES quad outline for one tile into a float vector.
  // Each edge = 2 verts = 6 floats → 4 edges = 8 verts = 24 floats per tile.
  auto pushTileLines = [&](std::vector<float>& buf, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= W || ty >= H) return;
    const float x0 = tx - 0.5f, x1 = static_cast<float>(tx) + 0.5f;
    const float z0 = ty - 0.5f, z1 = static_cast<float>(ty) + 0.5f;
    const float hSW = safeVH(H - ty,      tx)     + 0.05f;
    const float hSE = safeVH(H - ty,      tx + 1) + 0.05f;
    const float hNE = safeVH(H - ty - 1,  tx + 1) + 0.05f;
    const float hNW = safeVH(H - ty - 1,  tx)     + 0.05f;
    // Bottom edge SW→SE
    buf.insert(buf.end(), { x0, hSW, z0,  x1, hSE, z0 });
    // Right edge SE→NE
    buf.insert(buf.end(), { x1, hSE, z0,  x1, hNE, z1 });
    // Top edge NE→NW
    buf.insert(buf.end(), { x1, hNE, z1,  x0, hNW, z1 });
    // Left edge NW→SW
    buf.insert(buf.end(), { x0, hNW, z1,  x0, hSW, z0 });
  };

  const bool isRound = (brush_.shape == BrushShape::Round) && (szX > 1);
  hoverIsRound_ = isRound;

  if (!isRound) {
    // Square: single bounding-box LINE_LOOP (4 verts)
    const int half = szX / 2;
    const int bx0 = std::clamp(cx - half,           0, W - 1);
    const int by0 = std::clamp(cy - half,           0, H - 1);
    const int bx1 = std::clamp(cx - half + szX - 1, 0, W - 1);
    const int by1 = std::clamp(cy - half + szY - 1, 0, H - 1);

    const float verts[12] = {
      bx0 - 0.5f, safeVH(H - by0,     bx0)     + 0.05f, by0 - 0.5f,
      bx1 + 0.5f, safeVH(H - by0,     bx1 + 1) + 0.05f, by0 - 0.5f,
      bx1 + 0.5f, safeVH(H - by1 - 1, bx1 + 1) + 0.05f, by1 + 0.5f,
      bx0 - 0.5f, safeVH(H - by1 - 1, bx0)     + 0.05f, by1 + 0.5f,
    };
    hoverVertCount_ = 4;
    glNamedBufferSubData(hoverVbo_, 0, sizeof(verts), verts);
  } else {
    // Round: per-tile GL_LINES outlines for every tile in the brush mask
    const int half   = szX / 2;
    const float r    = static_cast<float>(half);
    std::vector<float> buf;
    buf.reserve(static_cast<std::size_t>(szX * szY * 24));
    for (int dy = -half; dy <= half; ++dy) {
      for (int dx = -half; dx <= half; ++dx) {
        const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        if (d > r + 0.5f) continue;
        pushTileLines(buf, cx + dx, cy + dy);
      }
    }
    hoverVertCount_ = static_cast<int>(buf.size() / 3);
    if (hoverVertCount_ > 0) {
      glNamedBufferSubData(hoverVbo_, 0,
                           static_cast<GLsizeiptr>(buf.size() * sizeof(float)),
                           buf.data());
    }
  }
}

// -----------------------------------------------------------------------
void EditorApp::resizeMap(int newW, int newH) {
  pushUndo();
  std::vector<std::vector<shared::TileData>> newTiles(
    static_cast<std::size_t>(newH),
    std::vector<shared::TileData>(static_cast<std::size_t>(newW)));
  for (int ty = 0; ty < newH; ++ty) {
    for (int tx = 0; tx < newW; ++tx) {
      if (ty < map_.height && tx < map_.width) { newTiles[ty][tx] = map_.tiles[ty][tx]; }
      else {
        auto& t = newTiles[ty][tx];
        t.x = tx; t.y = ty; t.walkable = true;
        t.groundColor = kDefaultGroundColor;
        t.type = shared::TileType::grass; t.obstacle = "";
      }
    }
  }
  map_.tiles = std::move(newTiles);

  const int oldVW = map_.width + 1, oldVH = map_.height + 1;
  const int newVW = newW + 1,       newVH = newH + 1;
  std::vector<float> newVH_arr(static_cast<std::size_t>(newVW * newVH), 0.0f);
  for (int vr = 0; vr < newVH && vr < oldVH; ++vr)
    for (int vc = 0; vc < newVW && vc < oldVW; ++vc)
      newVH_arr[static_cast<std::size_t>(vr * newVW + vc)] =
        map_.vertexHeights[static_cast<std::size_t>(vr * oldVW + vc)];
  map_.vertexHeights = std::move(newVH_arr);
  map_.width  = newW;
  map_.height = newH;
  map_.spawnPoint = { std::min(map_.spawnPoint[0], newW - 1), std::min(map_.spawnPoint[1], newH - 1) };
  npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
    [newW, newH](const shared::NpcSpawn& n){ return n.tileX >= newW || n.tileY >= newH; }),
    npcSpawns_.end());
  map_.overlayTiles.erase(std::remove_if(map_.overlayTiles.begin(), map_.overlayTiles.end(),
    [newW, newH](const shared::OverlayTile& o){ return o.tileX >= newW || o.tileY >= newH; }),
    map_.overlayTiles.end());

  rebuildTerrainGL(); rebuildObstacles();
  waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
  minimap_.init(newW, newH); minimap_.rebuild(map_, npcSpawns_);
}

// -----------------------------------------------------------------------
void EditorApp::pushUndo() {
  undo_.push(map_, npcSpawns_);
  dirty_ = true;
  updateWindowTitle();
}

void EditorApp::newMapDialog()    { showNewMapDialog_ = true; }

void EditorApp::updateWindowTitle() {
  const std::string filename = currentFilePath_.empty()
      ? "untitled"
      : std::filesystem::path(currentFilePath_).filename().string();
  const std::string title = (dirty_ ? "* " : "") + filename + " — Snook Editor";
  glfwSetWindowTitle(window_.handle(), title.c_str());
}

void EditorApp::addRecentFile(const std::string& path) {
  // Move to front, no duplicates
  recentFiles_.erase(std::remove(recentFiles_.begin(), recentFiles_.end(), path),
                     recentFiles_.end());
  recentFiles_.insert(recentFiles_.begin(), path);
  if (recentFiles_.size() > 10) recentFiles_.resize(10);
  saveRecentFiles();
}

void EditorApp::loadRecentFiles() {
  const auto cfgPath = resolveFromExe("recent_maps.txt");
  FILE* f = std::fopen(cfgPath.string().c_str(), "r");
  if (!f) return;
  char buf[MAX_PATH];
  while (std::fgets(buf, sizeof(buf), f)) {
    std::string line(buf);
    // Strip trailing newline
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();
    if (!line.empty()) recentFiles_.push_back(line);
  }
  std::fclose(f);
}

void EditorApp::saveRecentFiles() {
  const auto cfgPath = resolveFromExe("recent_maps.txt");
  FILE* f = std::fopen(cfgPath.string().c_str(), "w");
  if (!f) return;
  for (const auto& p : recentFiles_)
    std::fprintf(f, "%s\n", p.c_str());
  std::fclose(f);
}

void EditorApp::openFileDialog() {
  const std::wstring path = winOpenDialog();
  if (path.empty()) return;
  shared::WorldMapFile loaded;
  if (!shared::loadWorldMap(std::filesystem::path(path), loaded)) return;
  pushUndo();
  map_ = std::move(loaded);
  npcSpawns_ = map_.npcSpawns;
  currentFilePath_ = std::filesystem::path(path).string();
  dirty_ = false;
  addRecentFile(currentFilePath_);
  updateWindowTitle();
  rebuildTerrainGL(); rebuildObstacles();
  waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
  minimap_.init(map_.width, map_.height); minimap_.rebuild(map_, npcSpawns_);
  camera_.snapTo({ static_cast<float>(map_.width) * 0.5f, 0.0f,
                   static_cast<float>(map_.height) * 0.5f });
  worldRefreshNeighbors();
}

void EditorApp::openRecentFile(const std::string& path) {
  shared::WorldMapFile loaded;
  if (!shared::loadWorldMap(std::filesystem::path(path), loaded)) return;
  pushUndo();
  map_ = std::move(loaded);
  npcSpawns_ = map_.npcSpawns;
  currentFilePath_ = path;
  dirty_ = false;
  addRecentFile(currentFilePath_);
  updateWindowTitle();
  rebuildTerrainGL(); rebuildObstacles();
  waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
  minimap_.init(map_.width, map_.height); minimap_.rebuild(map_, npcSpawns_);
  camera_.snapTo({ static_cast<float>(map_.width) * 0.5f, 0.0f,
                   static_cast<float>(map_.height) * 0.5f });
  worldRefreshNeighbors();
}

void EditorApp::saveCurrentFile() {
  if (currentFilePath_.empty()) { saveAsDialog(); return; }
  map_.npcSpawns = npcSpawns_;
  shared::saveWorldMap(std::filesystem::path(currentFilePath_), map_);
  dirty_ = false;
  addRecentFile(currentFilePath_);
  updateWindowTitle();
}

void EditorApp::saveAsDialog() {
  const std::wstring path = winSaveDialog();
  if (path.empty()) return;
  currentFilePath_ = std::filesystem::path(path).string();
  saveCurrentFile();
}

// Returns the canonical maps directory (public/maps/ relative to the repo root,
// which is 3 levels above the exe: Release/ → build/ → client-native/ → root/).
// Falls back to empty string if the directory doesn't exist.
static std::wstring defaultMapsDir() {
  wchar_t exePath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  std::filesystem::path dir = std::filesystem::path(exePath).parent_path()
                              / L"../../../public/maps";
  std::error_code ec;
  dir = std::filesystem::canonical(dir, ec);
  if (ec || !std::filesystem::is_directory(dir, ec)) return {};
  return dir.wstring();
}

std::wstring EditorApp::winOpenDialog() {
  wchar_t buf[MAX_PATH] = {};
  const std::wstring mapsDir = defaultMapsDir();
  OPENFILENAMEW ofn = {};
  ofn.lStructSize    = sizeof(ofn);
  ofn.lpstrFilter    = L"JSON Map (*.json)\0*.json\0All Files\0*.*\0";
  ofn.lpstrFile      = buf; ofn.nMaxFile = MAX_PATH;
  ofn.lpstrInitialDir = mapsDir.empty() ? nullptr : mapsDir.c_str();
  ofn.Flags          = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  ofn.lpstrDefExt    = L"json";
  return GetOpenFileNameW(&ofn) ? buf : std::wstring{};
}

std::wstring EditorApp::winSaveDialog() {
  wchar_t buf[MAX_PATH] = {};
  const std::wstring mapsDir = defaultMapsDir();
  OPENFILENAMEW ofn = {};
  ofn.lStructSize    = sizeof(ofn);
  ofn.lpstrFilter    = L"JSON Map (*.json)\0*.json\0All Files\0*.*\0";
  ofn.lpstrFile      = buf; ofn.nMaxFile = MAX_PATH;
  ofn.lpstrInitialDir = mapsDir.empty() ? nullptr : mapsDir.c_str();
  ofn.Flags          = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  ofn.lpstrDefExt    = L"json";
  return GetSaveFileNameW(&ofn) ? buf : std::wstring{};
}

// -----------------------------------------------------------------------
void EditorApp::saveSettings() {
  AppSettings s;
  s.fogEnabled   = fogEnabled_;   s.fogDensity = fogDensity_;
  s.fogStart     = fogStart_;     s.fogR = fogColor_.r; s.fogG = fogColor_.g; s.fogB = fogColor_.b;
  s.aoEnabled    = aoEnabled_;    s.aoStrength = aoStrength_;
  s.lightingEnabled = lightingEnabled_;
  s.sunYawDeg = sunYawDeg_; s.sunPitchDeg = sunPitchDeg_;
  s.ambient   = ambient_;   s.diffuse     = diffuse_;
  s.shadowsEnabled   = shadowsEnabled_;
  s.shadowHalfExtent = shadowHalfExtent_;
  s.palette     = palette_;
  s.paletteHues = paletteHues_; s.paletteSats = paletteSats_; s.paletteLums = paletteLums_;
  // Water settings (shared with the game client).
  storeWaterSettings(waterUniforms_, s);
  // Outline fields are client-only; write defaults so the file is valid.
  ::saveSettings(s, resolveFromExe("settings.cfg"));
}

void EditorApp::loadSettings() {
  // (called from init; exposed as member for future use)
}

// -----------------------------------------------------------------------
void EditorApp::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigDebugHighlightIdConflicts = false;

  const auto fontPath = resolveFromExe("assets/ProggyClean.ttf");
  if (std::filesystem::exists(fontPath))
    io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 13.0f);
  else
    io.Fonts->AddFontDefault();

  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = s.FrameRounding = s.GrabRounding = s.ScrollbarRounding = 0.0f;
  s.TabRounding = s.PopupRounding = s.ChildRounding = 0.0f;
  s.WindowBorderSize = s.FrameBorderSize = 1.0f;
  s.ItemSpacing  = ImVec2(4, 4);
  s.FramePadding = ImVec2(5, 3);
  s.WindowPadding= ImVec2(6, 6);
  s.ScrollbarSize= 8.0f; s.GrabMinSize = 6.0f;

  ImVec4* c = s.Colors;
  c[ImGuiCol_Text]                 = ImVec4(0.94f, 0.82f, 0.50f, 1.00f);
  c[ImGuiCol_TextDisabled]         = ImVec4(0.54f, 0.44f, 0.25f, 1.00f);
  c[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.07f, 0.03f, 0.97f);
  c[ImGuiCol_ChildBg]              = ImVec4(0.09f, 0.06f, 0.02f, 0.80f);
  c[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.06f, 0.02f, 0.97f);
  c[ImGuiCol_Border]               = ImVec4(0.42f, 0.31f, 0.16f, 0.90f);
  c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg]              = ImVec4(0.07f, 0.04f, 0.01f, 0.90f);
  c[ImGuiCol_FrameBgHovered]       = ImVec4(0.15f, 0.09f, 0.03f, 0.90f);
  c[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  c[ImGuiCol_TitleBg]              = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_TitleBgActive]        = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.11f, 0.07f, 0.03f, 0.90f);
  c[ImGuiCol_MenuBarBg]            = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.03f, 0.01f, 0.80f);
  c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.42f, 0.31f, 0.16f, 0.90f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55f, 0.40f, 0.20f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.70f, 0.52f, 0.25f, 1.00f);
  c[ImGuiCol_CheckMark]            = ImVec4(1.00f, 0.65f, 0.15f, 1.00f);
  c[ImGuiCol_SliderGrab]           = ImVec4(1.00f, 0.65f, 0.15f, 0.85f);
  c[ImGuiCol_SliderGrabActive]     = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
  c[ImGuiCol_Button]               = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_ButtonHovered]        = ImVec4(0.42f, 0.26f, 0.10f, 1.00f);
  c[ImGuiCol_ButtonActive]         = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_Header]               = ImVec4(0.28f, 0.17f, 0.07f, 0.90f);
  c[ImGuiCol_HeaderHovered]        = ImVec4(0.42f, 0.26f, 0.10f, 0.90f);
  c[ImGuiCol_HeaderActive]         = ImVec4(0.55f, 0.34f, 0.14f, 1.00f);
  c[ImGuiCol_Separator]            = ImVec4(0.42f, 0.31f, 0.16f, 0.60f);
  c[ImGuiCol_SeparatorHovered]     = ImVec4(1.00f, 0.65f, 0.15f, 0.78f);
  c[ImGuiCol_SeparatorActive]      = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
  c[ImGuiCol_ResizeGrip]           = ImVec4(0.28f, 0.17f, 0.07f, 0.50f);
  c[ImGuiCol_ResizeGripHovered]    = ImVec4(1.00f, 0.65f, 0.15f, 0.78f);
  c[ImGuiCol_ResizeGripActive]     = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
  c[ImGuiCol_Tab]                  = ImVec4(0.15f, 0.09f, 0.03f, 0.95f);
  c[ImGuiCol_TabHovered]           = ImVec4(0.42f, 0.26f, 0.10f, 1.00f);
  c[ImGuiCol_TabActive]            = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.06f, 0.02f, 0.95f);
  c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  c[ImGuiCol_DockingPreview]       = ImVec4(1.00f, 0.65f, 0.15f, 0.70f);
  c[ImGuiCol_PlotLines]            = ImVec4(0.94f, 0.82f, 0.50f, 1.00f);
  c[ImGuiCol_PlotHistogram]        = ImVec4(1.00f, 0.65f, 0.15f, 1.00f);
  c[ImGuiCol_TableHeaderBg]        = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  c[ImGuiCol_TableBorderStrong]    = ImVec4(0.42f, 0.31f, 0.16f, 1.00f);
  c[ImGuiCol_TableBorderLight]     = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.04f);
  c[ImGuiCol_TextSelectedBg]       = ImVec4(1.00f, 0.65f, 0.15f, 0.35f);
  c[ImGuiCol_DragDropTarget]       = ImVec4(1.00f, 0.65f, 0.15f, 0.90f);
  c[ImGuiCol_NavHighlight]         = ImVec4(1.00f, 0.65f, 0.15f, 1.00f);
  c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.65f);

  ImGui_ImplGlfw_InitForOpenGL(window_.handle(), true);
  ImGui_ImplOpenGL3_Init("#version 460 core");
  imguiInited_ = true;
}

void EditorApp::shutdownImGui() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  imguiInited_ = false;
}

// ===========================================================================
// Database editor window
// ===========================================================================

void EditorApp::dbLoadAll() {
  try {
    dbItems_   = dbClient_.getItems();
    dbNPCs_    = dbClient_.getNPCs();
    dbObjects_ = dbClient_.getObjects();
    dbActions_ = dbClient_.getActions();
    dbSkills_  = dbClient_.getSkills();
    dbLoaded_  = true;
    dbStatus_  = "Loaded from server.";

    // Push object definitions into the obstacle system so sizeX/sizeY and
    // collision type are up to date for new custom objects.
    std::vector<world::ObstacleSystem::ObjectDefCache> caches;
    caches.reserve(dbObjects_.size());
    for (const auto& obj : dbObjects_) {
      world::ObstacleSystem::ObjectDefCache c;
      c.id           = obj.id;
      c.objectType   = obj.objectType;
      c.collision    = obj.collision;
      c.sizeX        = obj.sizeX;
      c.sizeY        = obj.sizeY;
      c.modelPath    = obj.modelPath;
      c.actionId     = obj.actionId;
      c.dropItemId   = obj.dropItemId;
      c.dropQuantity = obj.dropQuantity;
      c.respawnTicks = obj.respawnTicks;
      c.defaultClip  = obj.defaultClip;
      c.looping      = obj.looping;
      c.rotationX    = obj.rotationX;
      c.rotationY    = obj.rotationY;
      c.rotationZ    = obj.rotationZ;
      c.depletedObjectId = obj.depletedObjectId;
      c.pickable     = obj.pickable;
      caches.push_back(std::move(c));
    }
    obstacles_.rebuildFromDefinitions(caches);

    // Feed Wall/Pillar object defs (id → model) to the wall system so uploaded
    // meshes replace the placeholders.
    std::vector<std::pair<std::string, std::string>> wallDefs;
    for (const auto& obj : dbObjects_)
      if (obj.objectType == "Wall" || obj.objectType == "Pillar")
        wallDefs.emplace_back(obj.id, obj.modelPath);
    walls_.setWallDefs(wallDefs);
    walls_.rebuildFromMap(map_);

    // Load NPC models (or placeholder) so editor NPCs render like the game.
    entities_.setNpcModelResolver([](const std::string& rel) {
      return resolveFromExe(rel.c_str());
    });
    for (const auto& npc : dbNPCs_)
      entities_.ensureNpcModel(npc.id, npc.modelPath, npc.sizeX, npc.sizeY);

  } catch (const std::exception& e) {
    dbStatus_  = std::string("Load failed: ") + e.what();
    dbLoaded_  = false;
  }
}

// ---- Helper: small text input that writes into a std::string
static bool dbInputText(const char* label, std::string& s, float width = -1.0f) {
  char buf[512];
  std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  if (width > 0.0f) ImGui::SetNextItemWidth(width);
  else              ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText(label, buf, sizeof(buf))) { s = buf; return true; }
  return false;
}

// Open a Windows file dialog, copy the chosen file into <destSubdir> next to the
// exe (so editor + game share it), and return the relative path
// ("assets/.../<file>"). Returns empty on cancel. destSubdir must end in '/'.
static std::string dbBrowseCopyAsset(const wchar_t* filter, const std::string& destSubdir) {
  OPENFILENAMEW ofn = {};
  wchar_t buf[MAX_PATH] = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = filter;
  ofn.lpstrFile   = buf;  ofn.nMaxFile = MAX_PATH;
  ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return {};

  const int sz = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
  std::string src(static_cast<std::size_t>(sz), '\0');
  WideCharToMultiByte(CP_UTF8, 0, buf, -1, src.data(), sz, nullptr, nullptr);
  if (!src.empty() && src.back() == '\0') src.pop_back();

  const std::filesystem::path s(src);
  const std::string rel = destSubdir + s.filename().string();

  // Copy into the build output (next to the exe) so it loads immediately, AND
  // into the source asset tree (client-native/assets, two levels above the exe)
  // so it's committed to git and survives clean rebuilds. Both best-effort.
  std::error_code ec;
  const auto buildDest = resolveFromExe(rel.c_str());
  std::filesystem::create_directories(buildDest.parent_path(), ec);
  std::filesystem::copy_file(s, buildDest, std::filesystem::copy_options::overwrite_existing, ec);

  std::error_code ec2;
  const auto srcDest = resolveFromExe(("../../" + rel).c_str());  // build/Release -> client-native
  std::filesystem::create_directories(srcDest.parent_path(), ec2);
  std::filesystem::copy_file(s, srcDest, std::filesystem::copy_options::overwrite_existing, ec2);

  return ec ? src : rel;   // fall back to absolute source if the build copy failed
}

static bool dbCombo(const char* label, std::string& val, std::initializer_list<const char*> opts) {
  bool changed = false;
  if (ImGui::BeginCombo(label, val.c_str())) {
    for (const char* o : opts) {
      bool sel = (val == o);
      if (ImGui::Selectable(o, sel)) { val = o; changed = true; }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

// ---- Items tab -------------------------------------------------------------

void EditorApp::dbDrawItemsTab() {
  // Left: list
  ImGui::BeginChild("##item_list", ImVec2(200, 0), true);
  if (ImGui::Button("+ New Item", ImVec2(-1, 0))) {
    dbEditItem_   = ItemDef{};
    dbSelItem_    = -1;
    dbEditIsNew_  = true;
  }
  ImGui::Separator();
  for (int i = 0; i < (int)dbItems_.size(); ++i) {
    bool sel = (dbSelItem_ == i);
    if (ImGui::Selectable(dbItems_[i].id.c_str(), sel)) {
      dbSelItem_   = i;
      dbEditItem_  = dbItems_[i];
      dbEditIsNew_ = false;
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // Right: edit form
  ImGui::BeginChild("##item_edit", ImVec2(0, 0), false);
  if (dbSelItem_ >= 0 || dbEditIsNew_) {
    ItemDef& d = dbEditItem_;

    ImGui::TextDisabled("ID (immutable after creation)");
    if (dbEditIsNew_) { ImGui::SetNextItemWidth(-1); dbInputText("##item_id", d.id); }
    else              ImGui::TextUnformatted(d.id.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1); dbInputText("##item_name", d.name);

    ImGui::Separator();
    dbCombo("Type##item", d.itemType, {"resource", "equipment", "food"});
    ImGui::Checkbox("Stackable", &d.stackable); ImGui::SameLine();
    ImGui::Checkbox("Tradable",  &d.tradable);
    ImGui::SetNextItemWidth(120); ImGui::InputInt("Value##item", &d.value);

    if (d.itemType == "equipment") {
      ImGui::Separator();
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Equipment");
      dbCombo("Equip Slot##item",   d.equipSlot,
        {"head","body","legs","feet","hands","neck","ring","leftHand","rightHand","ammo"});
      ImGui::Checkbox("Two-Handed##item", &d.twoHanded);
      dbCombo("Combat Style##item", d.combatStyle, {"","melee","gunner"});
      dbCombo("Tool Type##item",    d.toolType,    {"","axe","pickaxe"});
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Stats");
      ImGui::SetNextItemWidth(80); ImGui::InputInt("M.Atk##i",  &d.meleeAttack);  ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("M.Str##i",  &d.meleeStrength); ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("M.Def##i",  &d.meleeDefense);
      ImGui::SetNextItemWidth(80); ImGui::InputInt("R.Atk##i",  &d.rangedAttack); ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("R.Str##i",  &d.rangedStrength); ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("R.Def##i",  &d.rangedDefense);
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Requirement");
      dbCombo("Skill##item_req", d.requiredSkill,
        {"","woodcutting","mining","warrior","defence","hitpoints","gunner"});
      ImGui::SameLine(); ImGui::SetNextItemWidth(60);
      ImGui::InputInt("Level##item_req", &d.requiredLevel);
    }
    if (d.itemType == "food") {
      ImGui::Separator();
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Food");
      ImGui::SetNextItemWidth(80); ImGui::InputInt("Heal HP##item", &d.healAmount);
    }

    ImGui::Separator();
    ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Assets");
    ImGui::TextUnformatted("Sprite Path");
    ImGui::SetNextItemWidth(-1); dbInputText("##item_sprite", d.spritePath);
    if (ImGui::Button("Browse Sprite...##item_sprite", ImVec2(-1, 0))) {
      std::string rel = dbBrowseCopyAsset(L"PNG Image (*.png)\0*.png\0All Files\0*.*\0",
                                          "assets/sprites/items/");
      if (!rel.empty()) d.spritePath = rel;
    }
    ImGui::TextUnformatted("Dropped Model");
    ImGui::SetNextItemWidth(-1); dbInputText("##item_dropped", d.modelDropped);
    if (ImGui::Button("Browse Dropped Model...##item_dropped", ImVec2(-1, 0))) {
      std::string rel = dbBrowseCopyAsset(L"3D Model (*.glb;*.gltf)\0*.glb;*.gltf\0All Files\0*.*\0",
                                          "assets/models/");
      if (!rel.empty()) d.modelDropped = rel;
    }
    ImGui::TextUnformatted("Equipped Model");
    ImGui::SetNextItemWidth(-1); dbInputText("##item_equipped", d.modelEquipped);
    if (ImGui::Button("Browse Equipped Model...##item_equipped", ImVec2(-1, 0))) {
      std::string rel = dbBrowseCopyAsset(L"3D Model (*.glb;*.gltf)\0*.glb;*.gltf\0All Files\0*.*\0",
                                          "assets/models/");
      if (!rel.empty()) d.modelEquipped = rel;
    }

    // ---- Held-weapon grip (only meaningful with an equipped model) ----------
    // Values are RELATIVE to the current player model's hand bone. The preview
    // (top-right) shows the player holding the weapon; tune, then Save.
    if (!d.modelEquipped.empty()) {
      ImGui::Separator();
      ImGui::TextDisabled("Held-weapon grip (model-relative)");
      ImGui::Checkbox("Preview in hand##grip", &gripPreview_);
      // Live preview of the player holding the weapon (rendered into the shared
      // preview FBO by dbRenderPreview's grip branch). FBO is bottom-up → flip V.
      if (gripPreview_ && dbPreviewTex_) {
        // Reserve the area with an InvisibleButton so dragging orbits the camera
        // and CAPTURES the mouse (otherwise ImGui drags the window instead);
        // draw the FBO texture into that rect via the draw list.
        const ImVec2 sz(256, 256);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##grip_view", sz);
        const bool active = ImGui::IsItemActive();
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)dbPreviewTex_, p0,
            ImVec2(p0.x + sz.x, p0.y + sz.y), ImVec2(0, 1), ImVec2(1, 0));
        if (active) {
          const ImVec2 md = ImGui::GetIO().MouseDelta;
          gripYaw_   += md.x * 0.012f;
          gripPitch_  = std::clamp(gripPitch_ + md.y * 0.012f, -1.4f, 1.4f);
        }
        ImGui::TextDisabled("Drag to rotate");
        if (!playerPreview_.isLoaded() && playerPreviewTried_)
          ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "player.glb failed to load");
      }
      ImGui::TextUnformatted("Attach joint (blank = default hand)");
      ImGui::SetNextItemWidth(-1); dbInputText("##grip_joint", d.gripJoint);
      ImGui::SetNextItemWidth(-1); ImGui::DragFloat3("Pos##grip",   &d.gripPosX, 0.005f);
      ImGui::SetNextItemWidth(-1); ImGui::DragFloat3("Rot\xC2\xB0##grip", &d.gripRotX, 1.0f);
      ImGui::SetNextItemWidth(-1); ImGui::DragFloat ("Scale##grip", &d.gripScale, 0.01f, 0.01f, 20.0f);
      // Player clip used in the preview (so you can tune against the attack pose).
      if (playerPreview_.isLoaded()) {
        const char* cur = (gripClipIndex_ >= 0)
            ? (playerPreview_.animationNameAt(gripClipIndex_) ? playerPreview_.animationNameAt(gripClipIndex_)->c_str() : "?")
            : "?";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##grip_clip", cur)) {
          for (int c = 0; c < playerPreview_.animationCount(); ++c) {
            const std::string* nm = playerPreview_.animationNameAt(c);
            if (!nm) continue;
            if (ImGui::Selectable(nm->c_str(), c == gripClipIndex_)) gripClipIndex_ = c;
          }
          ImGui::EndCombo();
        }
      }
    }

    ImGui::TextUnformatted("Examine Text");
    ImGui::SetNextItemWidth(-1); dbInputText("##item_examine", d.examineText);

    ImGui::Separator();
    if (!dbStatus_.empty()) ImGui::TextDisabled("%s", dbStatus_.c_str());

    if (ImGui::Button("Save##item")) {
      if (dbClient_.saveItem(d, dbEditIsNew_)) {
        dbStatus_ = "Saved.";
        dbLoadAll();
        // Re-select by id
        for (int i = 0; i < (int)dbItems_.size(); ++i)
          if (dbItems_[i].id == d.id) { dbSelItem_ = i; dbEditIsNew_ = false; break; }
      } else { dbStatus_ = "Save failed: " + dbClient_.lastError; }
    }
    ImGui::SameLine();
    if (!dbEditIsNew_ && ImGui::Button("Delete##item")) {
      if (dbClient_.deleteItem(d.id)) {
        dbStatus_ = "Deleted.";
        dbSelItem_ = -1; dbLoadAll();
      } else { dbStatus_ = "Delete failed: " + dbClient_.lastError; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert##item")) {
      if (dbSelItem_ >= 0) dbEditItem_ = dbItems_[dbSelItem_];
    }
  } else {
    ImGui::TextDisabled("Select an item or click '+ New Item'.");
  }
  ImGui::EndChild();
}

// ---- Skills tab ------------------------------------------------------------
// Skills are a fixed set (mirrors SkillId); the editor only authors the icon
// and display name, so there is no create/delete — just select + Save (PUT).

void EditorApp::dbDrawSkillsTab() {
  ImGui::BeginChild("##skill_list", ImVec2(200, 0), true);
  for (int i = 0; i < (int)dbSkills_.size(); ++i) {
    bool sel = (dbSelSkill_ == i);
    const auto& s = dbSkills_[i];
    const char* label = s.name.empty() ? s.id.c_str() : s.name.c_str();
    if (ImGui::Selectable(label, sel)) {
      dbSelSkill_  = i;
      dbEditSkill_ = dbSkills_[i];
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##skill_edit", ImVec2(0, 0), false);
  if (dbSelSkill_ >= 0) {
    SkillDef& d = dbEditSkill_;

    ImGui::TextDisabled("Skill ID (fixed)");
    ImGui::TextUnformatted(d.id.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1); dbInputText("##skill_name", d.name);

    ImGui::Separator();
    ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Icon");
    ImGui::TextUnformatted("Icon Path");
    ImGui::SetNextItemWidth(-1); dbInputText("##skill_icon", d.iconPath);
    if (ImGui::Button("Browse Icon...##skill_icon", ImVec2(-1, 0))) {
      std::string rel = dbBrowseCopyAsset(L"PNG Image (*.png)\0*.png\0All Files\0*.*\0",
                                          "assets/sprites/skills/");
      if (!rel.empty()) d.iconPath = rel;
    }
    ImGui::TextDisabled("Authored as a 32x32 PNG (like item sprites).");

    ImGui::Separator();
    if (!dbStatus_.empty()) ImGui::TextDisabled("%s", dbStatus_.c_str());

    if (ImGui::Button("Save##skill")) {
      if (dbClient_.saveSkill(d)) {
        dbStatus_ = "Saved.";
        dbLoadAll();
        for (int i = 0; i < (int)dbSkills_.size(); ++i)
          if (dbSkills_[i].id == d.id) { dbSelSkill_ = i; break; }
      } else { dbStatus_ = "Save failed: " + dbClient_.lastError; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert##skill")) {
      if (dbSelSkill_ >= 0) dbEditSkill_ = dbSkills_[dbSelSkill_];
    }
  } else {
    ImGui::TextDisabled("Select a skill to set its icon.");
  }
  ImGui::EndChild();
}

// ---- NPCs tab --------------------------------------------------------------

void EditorApp::dbDrawNPCsTab() {
  ImGui::BeginChild("##npc_list", ImVec2(200, 0), true);
  if (ImGui::Button("+ New NPC", ImVec2(-1, 0))) {
    dbEditNPC_  = NpcDef{};
    dbSelNPC_   = -1;
    dbEditIsNew_= true;
  }
  ImGui::Separator();
  for (int i = 0; i < (int)dbNPCs_.size(); ++i) {
    bool sel = (dbSelNPC_ == i);
    if (ImGui::Selectable(dbNPCs_[i].id.c_str(), sel)) {
      dbSelNPC_   = i;
      dbEditNPC_  = dbNPCs_[i];
      dbEditIsNew_= false;
      dbLoadPreviewModel(dbNPCs_[i].modelPath);
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##npc_edit", ImVec2(0, 0), false);
  if (dbSelNPC_ >= 0 || dbEditIsNew_) {
    NpcDef& d = dbEditNPC_;

    // 3D model preview
    if (dbPreviewTex_) {
      // Flip V — the FBO texture is bottom-up (OpenGL origin), same as the
      // main 3D viewport which uses (0,1)-(1,0).
      ImGui::Image((ImTextureID)(intptr_t)dbPreviewTex_, ImVec2(128, 128),
                   ImVec2(0, 1), ImVec2(1, 0));
      ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::TextDisabled("ID");
    if (dbEditIsNew_) { ImGui::SetNextItemWidth(-1); dbInputText("##npc_id", d.id); }
    else              ImGui::TextUnformatted(d.id.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1); dbInputText("##npc_name", d.name);
    ImGui::SetNextItemWidth(80); ImGui::InputInt("Size X##npc", &d.sizeX); ImGui::SameLine();
    ImGui::SetNextItemWidth(80); ImGui::InputInt("Size Y##npc", &d.sizeY);
    dbCombo("AI##npc", d.ai, {"static", "wander"});
    ImGui::EndGroup();  // closes the group started beside the preview image

    ImGui::TextUnformatted("Model Path");
    ImGui::SetNextItemWidth(-1); dbInputText("##npc_model", d.modelPath);
    if (ImGui::Button("Browse Model...##npc_model", ImVec2(-1, 0))) {
      OPENFILENAMEW ofn = {};
      wchar_t buf[MAX_PATH] = {};
      ofn.lStructSize = sizeof(ofn);
      ofn.lpstrFilter = L"3D Model (*.glb;*.gltf)\0*.glb;*.gltf\0All Files\0*.*\0";
      ofn.lpstrFile   = buf; ofn.nMaxFile = MAX_PATH;
      ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
      if (GetOpenFileNameW(&ofn)) {
        wchar_t exeDir[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        std::filesystem::path rel = std::filesystem::relative(buf, std::filesystem::path(exeDir).parent_path());
        d.modelPath = rel.string();
        dbLoadPreviewModel(d.modelPath);
      }
    }
    if (d.modelPath != dbPreviewLoadedPath_) dbLoadPreviewModel(d.modelPath);
    ImGui::TextUnformatted("Examine Text");
    ImGui::SetNextItemWidth(-1); dbInputText("##npc_examine", d.examineText);

    ImGui::Separator();
    ImGui::Checkbox("Attackable##npc", &d.isAttackable);
    if (d.isAttackable) {
      ImGui::Indent();
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Combat Stats");
      ImGui::SetNextItemWidth(80); ImGui::InputInt("Max HP##npc",      &d.maxHp);      ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("Attack##npc",      &d.attack);     ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("Strength##npc",    &d.strength);
      ImGui::SetNextItemWidth(80); ImGui::InputInt("M.Def##npc",       &d.meleeDefense); ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("R.Def##npc",       &d.rangedDefense); ImGui::SameLine();
      ImGui::SetNextItemWidth(80); ImGui::InputInt("Spd Ticks##npc",   &d.attackSpeedTicks);
      ImGui::SetNextItemWidth(100); ImGui::InputInt("Respawn Ticks##npc", &d.respawnTicks);

      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Drop Table");
      // Drop table
      int removeDrop = -1;
      for (int i = 0; i < (int)d.drops.size(); ++i) {
        DropEntry& dr = d.drops[i];
        ImGui::PushID(i);
        char dbuf[64]; std::strncpy(dbuf, dr.itemId.c_str(), sizeof(dbuf)-1); dbuf[sizeof(dbuf)-1]='\0';
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("##drop_item", dbuf, sizeof(dbuf))) dr.itemId = dbuf;
        ImGui::SameLine(); ImGui::SetNextItemWidth(50);
        ImGui::InputInt("##drop_qty", &dr.quantity);
        ImGui::SameLine(); ImGui::SetNextItemWidth(60);
        ImGui::InputFloat("##drop_rate", &dr.rate, 0.f, 0.f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) removeDrop = i;
        ImGui::PopID();
      }
      if (removeDrop >= 0) d.drops.erase(d.drops.begin() + removeDrop);
      if (ImGui::Button("+ Drop")) d.drops.push_back({"", 1, 1.0f});
      ImGui::Unindent();
    }

    ImGui::Checkbox("Talkable##npc", &d.isTalkable);
    if (d.isTalkable) {
      ImGui::Indent();
      dbInputText("Dialogue##npc", d.dialogue);
      ImGui::Unindent();
    }

    ImGui::Separator();
    if (!dbStatus_.empty()) ImGui::TextDisabled("%s", dbStatus_.c_str());
    if (ImGui::Button("Save##npc")) {
      if (dbClient_.saveNPC(d, dbEditIsNew_)) {
        dbStatus_ = "Saved."; dbLoadAll();
        for (int i = 0; i < (int)dbNPCs_.size(); ++i)
          if (dbNPCs_[i].id == d.id) { dbSelNPC_ = i; dbEditIsNew_ = false; break; }
      } else { dbStatus_ = "Save failed: " + dbClient_.lastError; }
    }
    ImGui::SameLine();
    if (!dbEditIsNew_ && ImGui::Button("Delete##npc")) {
      if (dbClient_.deleteNPC(d.id)) { dbStatus_ = "Deleted."; dbSelNPC_ = -1; dbLoadAll(); }
      else dbStatus_ = "Delete failed: " + dbClient_.lastError;
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert##npc")) { if (dbSelNPC_ >= 0) dbEditNPC_ = dbNPCs_[dbSelNPC_]; }
  } else {
    ImGui::TextDisabled("Select an NPC or click '+ New NPC'.");
  }
  ImGui::EndChild();
}

// ---- Objects tab -----------------------------------------------------------

void EditorApp::dbDrawObjectsTab() {
  ImGui::BeginChild("##obj_list", ImVec2(200, 0), true);
  if (ImGui::Button("+ New Object", ImVec2(-1, 0))) {
    dbEditObject_ = ObjectDef{};
    dbSelObject_  = -1;
    dbEditIsNew_  = true;
  }
  ImGui::Separator();
  for (int i = 0; i < (int)dbObjects_.size(); ++i) {
    bool sel = (dbSelObject_ == i);
    if (ImGui::Selectable(dbObjects_[i].id.c_str(), sel)) {
      dbSelObject_  = i;
      dbEditObject_ = dbObjects_[i];
      dbEditIsNew_  = false;
      dbLoadPreviewModel(dbObjects_[i].modelPath);
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##obj_edit", ImVec2(0, 0), false);
  if (dbSelObject_ >= 0 || dbEditIsNew_) {
    ObjectDef& d = dbEditObject_;

    // 3D model preview
    if (dbPreviewTex_) {
      // Flip V — the FBO texture is bottom-up (OpenGL origin), same as the
      // main 3D viewport which uses (0,1)-(1,0).
      ImGui::Image((ImTextureID)(intptr_t)dbPreviewTex_, ImVec2(128, 128),
                   ImVec2(0, 1), ImVec2(1, 0));
      ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::TextDisabled("ID");
    if (dbEditIsNew_) { ImGui::SetNextItemWidth(-1); dbInputText("##obj_id", d.id); }
    else              ImGui::TextUnformatted(d.id.c_str());
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1); dbInputText("##obj_name", d.name);
    dbCombo("Type##obj",      d.objectType, {"Decoration", "ResourceNode", "ProductionFacility", "Wall", "Pillar"});
    dbCombo("Collision##obj", d.collision,  {"none", "full_blocking", "half_blocking"});
    ImGui::SetNextItemWidth(80); ImGui::InputInt("Size X##obj", &d.sizeX); ImGui::SameLine();
    ImGui::SetNextItemWidth(80); ImGui::InputInt("Size Y##obj", &d.sizeY);
    ImGui::EndGroup();  // closes the group beside the preview image

    ImGui::TextUnformatted("Model Path");
    ImGui::SetNextItemWidth(-1); dbInputText("##obj_model", d.modelPath);
    if (ImGui::Button("Browse Model...##obj_model", ImVec2(-1, 0))) {
      OPENFILENAMEW ofn = {};
      wchar_t buf[MAX_PATH] = {};
      ofn.lStructSize = sizeof(ofn);
      ofn.lpstrFilter = L"3D Model (*.glb;*.gltf)\0*.glb;*.gltf\0All Files\0*.*\0";
      ofn.lpstrFile   = buf; ofn.nMaxFile = MAX_PATH;
      ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
      if (GetOpenFileNameW(&ofn)) {
        // Convert the chosen path to UTF-8.
        const int sz = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
        std::string srcPath(static_cast<std::size_t>(sz), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, srcPath.data(), sz, nullptr, nullptr);
        if (!srcPath.empty() && srcPath.back() == '\0') srcPath.pop_back();

        // Copy the model into assets/models/<filename> so both the editor and
        // the game client (same Release dir) can load it, then store the
        // relative path. Force-reload the preview even if the path is unchanged.
        const std::filesystem::path src(srcPath);
        const std::string fname   = src.filename().string();
        const std::string relPath = "assets/models/" + fname;
        const auto destPath = resolveFromExe(relPath.c_str());
        std::error_code ec;
        std::filesystem::create_directories(destPath.parent_path(), ec);
        std::filesystem::copy_file(src, destPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        // Also copy into the source asset tree (client-native/assets) so it's
        // committed + survives clean rebuilds.
        std::error_code ec2;
        const auto srcDest = resolveFromExe(("../../" + relPath).c_str());
        std::filesystem::create_directories(srcDest.parent_path(), ec2);
        std::filesystem::copy_file(src, srcDest,
            std::filesystem::copy_options::overwrite_existing, ec2);
        d.modelPath = ec ? srcPath : relPath;
        dbLoadPreviewModel(d.modelPath, /*forceReload=*/true);
      }
    }
    if (d.modelPath != dbPreviewLoadedPath_) dbLoadPreviewModel(d.modelPath);
    if (ImGui::Button("Reload Model##obj_model", ImVec2(-1, 0)))
      dbLoadPreviewModel(d.modelPath, /*forceReload=*/true);

    // Pickable — hover outline + left-click. Default true; set false for pure
    // decorations (e.g. a tree stump used only as a depleted variant).
    ImGui::Spacing();
    ImGui::Checkbox("Pickable##obj", &d.pickable);

    // Depleted object — another object shown in-game while this resource node is
    // depleted (between harvest and respawn). Empty = render nothing. Picking,
    // outline and examine then follow that object (e.g. a non-pickable stump).
    ImGui::TextUnformatted("Depleted Object (optional)");
    const char* depPreview = d.depletedObjectId.empty() ? "(none)" : d.depletedObjectId.c_str();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##obj_depleted_obj", depPreview)) {
      if (ImGui::Selectable("(none)", d.depletedObjectId.empty()))
        d.depletedObjectId.clear();
      for (const auto& o : dbObjects_) {
        if (o.id == d.id) continue;   // can't reference itself
        const bool sel = (o.id == d.depletedObjectId);
        if (ImGui::Selectable(o.id.c_str(), sel)) d.depletedObjectId = o.id;
      }
      ImGui::EndCombo();
    }

    // ---- Animation section (only shown when the loaded model has clips) ----
    if (dbPreviewHasAnim_ && !dbPreviewClips_.empty()) {
      ImGui::Separator();
      ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "Animation");
      // Clip selector
      const char* currentClip = d.defaultClip.empty() ? "(first clip)" : d.defaultClip.c_str();
      if (ImGui::BeginCombo("Clip##obj_anim", currentClip)) {
        if (ImGui::Selectable("(first clip)", d.defaultClip.empty())) {
          d.defaultClip = "";
          dbPreviewSkinned_.setClip("");
        }
        for (const auto& clip : dbPreviewClips_) {
          bool sel = (d.defaultClip == clip);
          if (ImGui::Selectable(clip.c_str(), sel)) {
            d.defaultClip = clip;
            dbPreviewSkinned_.setClip(clip);
          }
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::Checkbox("Loop##obj_anim", &d.looping);
    }

    // Orientation is authored in the model file (true scale, base at Y=0,
    // facing −Z). The preview shows it exactly as it renders in-world.

    ImGui::TextUnformatted("Examine Text");
    ImGui::SetNextItemWidth(-1); dbInputText("##obj_examine", d.examineText);

    if (d.objectType == "ResourceNode") {
      ImGui::Separator();
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Resource Node");
      // Action combo from loaded actions
      if (ImGui::BeginCombo("Action##obj", d.actionId.c_str())) {
        if (ImGui::Selectable("(none)", d.actionId.empty())) d.actionId = "";
        for (const auto& a : dbActions_) {
          bool sel = (d.actionId == a.id);
          if (ImGui::Selectable(a.displayName.c_str(), sel)) d.actionId = a.id;
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      dbCombo("Req. Skill##obj", d.requiredSkill,
        {"","woodcutting","mining","warrior","defence","hitpoints","gunner"});
      ImGui::SameLine(); ImGui::SetNextItemWidth(60);
      ImGui::InputInt("Level##obj", &d.requiredLevel);
      // Drop item combo from loaded items
      if (ImGui::BeginCombo("Drop Item##obj", d.dropItemId.c_str())) {
        if (ImGui::Selectable("(none)", d.dropItemId.empty())) d.dropItemId = "";
        for (const auto& it : dbItems_) {
          bool sel = (d.dropItemId == it.id);
          if (ImGui::Selectable(it.name.c_str(), sel)) d.dropItemId = it.id;
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine(); ImGui::SetNextItemWidth(60);
      ImGui::InputInt("Qty##obj",         &d.dropQuantity);
      ImGui::SetNextItemWidth(120); ImGui::InputInt("Respawn Ticks##obj", &d.respawnTicks);
    }
    if (d.objectType == "ProductionFacility") {
      ImGui::Separator();
      ImGui::TextColored({1.f,0.55f,0.f,1.f}, "Production Facility");
      if (ImGui::BeginCombo("Craft Action##obj", d.craftActionId.c_str())) {
        if (ImGui::Selectable("(none)", d.craftActionId.empty())) d.craftActionId = "";
        for (const auto& a : dbActions_) {
          bool sel = (d.craftActionId == a.id);
          if (ImGui::Selectable(a.displayName.c_str(), sel)) d.craftActionId = a.id;
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::Separator();
    if (!dbStatus_.empty()) ImGui::TextDisabled("%s", dbStatus_.c_str());
    if (ImGui::Button("Save##obj")) {
      if (dbClient_.saveObject(d, dbEditIsNew_)) {
        dbStatus_ = "Saved."; dbLoadAll();
        for (int i = 0; i < (int)dbObjects_.size(); ++i)
          if (dbObjects_[i].id == d.id) { dbSelObject_ = i; dbEditIsNew_ = false; break; }
      } else { dbStatus_ = "Save failed: " + dbClient_.lastError; }
    }
    ImGui::SameLine();
    if (!dbEditIsNew_ && ImGui::Button("Delete##obj")) {
      if (dbClient_.deleteObject(d.id)) { dbStatus_ = "Deleted."; dbSelObject_ = -1; dbLoadAll(); }
      else dbStatus_ = "Delete failed: " + dbClient_.lastError;
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert##obj")) { if (dbSelObject_ >= 0) dbEditObject_ = dbObjects_[dbSelObject_]; }
  } else {
    ImGui::TextDisabled("Select an object or click '+ New Object'.");
  }
  ImGui::EndChild();
}

// ---- Actions tab -----------------------------------------------------------

void EditorApp::dbDrawActionsTab() {
  ImGui::BeginChild("##act_list", ImVec2(200, 0), true);
  if (ImGui::Button("+ New Action", ImVec2(-1, 0))) {
    dbEditAction_ = ActionDef{};
    dbSelAction_  = -1;
    dbEditIsNew_  = true;
  }
  ImGui::Separator();
  for (int i = 0; i < (int)dbActions_.size(); ++i) {
    bool sel = (dbSelAction_ == i);
    char lbl[128];
    std::snprintf(lbl, sizeof(lbl), "%s  (%s)", dbActions_[i].displayName.c_str(), dbActions_[i].id.c_str());
    if (ImGui::Selectable(lbl, sel)) {
      dbSelAction_  = i;
      dbEditAction_ = dbActions_[i];
      dbEditIsNew_  = false;
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##act_edit", ImVec2(0, 0), false);
  if (dbSelAction_ >= 0 || dbEditIsNew_) {
    ActionDef& d = dbEditAction_;
    ImGui::TextDisabled("ID (slug, e.g. 'chop')");
    if (dbEditIsNew_) { ImGui::SetNextItemWidth(-1); dbInputText("##act_id", d.id); }
    else              ImGui::TextUnformatted(d.id.c_str());
    ImGui::TextUnformatted("Display Name");
    ImGui::SetNextItemWidth(-1); dbInputText("##act_name", d.displayName);
    dbCombo("Handler Type##act", d.handlerType,
      {"gather_resource", "production_facility", "equip", "eat", "talk", "bank", "examine"});

    ImGui::TextDisabled(
      "Handler type controls server behavior.\n"
      "Display name is what the player sees in the context menu.");

    ImGui::Separator();
    if (!dbStatus_.empty()) ImGui::TextDisabled("%s", dbStatus_.c_str());
    if (ImGui::Button("Save##act")) {
      if (dbClient_.saveAction(d, dbEditIsNew_)) {
        dbStatus_ = "Saved."; dbLoadAll();
        for (int i = 0; i < (int)dbActions_.size(); ++i)
          if (dbActions_[i].id == d.id) { dbSelAction_ = i; dbEditIsNew_ = false; break; }
      } else { dbStatus_ = "Save failed: " + dbClient_.lastError; }
    }
    ImGui::SameLine();
    if (!dbEditIsNew_ && ImGui::Button("Delete##act")) {
      if (dbClient_.deleteAction(d.id)) { dbStatus_ = "Deleted."; dbSelAction_ = -1; dbLoadAll(); }
      else dbStatus_ = "Delete failed: " + dbClient_.lastError;
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert##act")) { if (dbSelAction_ >= 0) dbEditAction_ = dbActions_[dbSelAction_]; }
  } else {
    ImGui::TextDisabled("Select an action or click '+ New Action'.");
  }
  ImGui::EndChild();
}

// ---- Main window -----------------------------------------------------------

void EditorApp::drawDatabaseWindow() {
  // Lazy init of preview FBO (requires GL context to be current)
  if (!dbPreviewFbo_) dbInitPreviewFbo();

  // Fixed workspace pane: renderFrame pins position/size to the content area
  // right of the mode rail (Database mode). No title bar / close button — the
  // rail switches modes.
  const ImGuiWindowFlags paneFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoDocking  | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_MenuBar;
  if (!ImGui::Begin("##database", nullptr, paneFlags)) {
    ImGui::End();
    return;
  }

  // Menu bar inside the window
  if (ImGui::BeginMenuBar()) {
    if (ImGui::MenuItem("Refresh")) dbLoadAll();
    if (!dbStatus_.empty()) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f);
      ImGui::TextDisabled("%s", dbStatus_.c_str());
    }
    ImGui::EndMenuBar();
  }

  if (!dbLoaded_) {
    ImGui::TextColored({1,0.4f,0.4f,1}, "Not connected to server. Start the server, then click Refresh.");
    if (ImGui::Button("Refresh Now")) dbLoadAll();
    ImGui::End();
    return;
  }

  // Tabs
  if (ImGui::BeginTabBar("##db_tabs")) {
    if (ImGui::BeginTabItem("Items")) {
      dbDrawItemsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("NPCs")) {
      dbDrawNPCsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Objects")) {
      dbDrawObjectsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Actions")) {
      dbDrawActionsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Skills")) {
      dbDrawSkillsTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

}  // namespace editor
