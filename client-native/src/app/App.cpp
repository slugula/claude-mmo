#include "app/App.hpp"

#include "editor/EntityClient.hpp"
#include "editor/EntityDefs.hpp"
#include "render/GlDebug.hpp"
#include "ui/NameRegistry.hpp"
#include "ui/ClayRenderer.hpp"
#include "ui/MinimapRenderer.hpp"
#include "ui/ClayContextMenu.hpp"
#include "ui/ClayClickFeedback.hpp"
#include "ui/ClayChatLog.hpp"
#include "ui/ClayLoginModal.hpp"
#include "ui/ClayBankPanel.hpp"
#include "shared/SharedTypesJson.hpp"
#include "world/GltfLoader.hpp"
#include "world/MapGenerator.hpp"
#include "world/TerrainBuilder.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/gtc/matrix_transform.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace app {

namespace {
constexpr int kInitialWidth  = 1280;
constexpr int kInitialHeight = 720;
constexpr int kMsaaSamples   = 4;
constexpr int kMapWidth      = 64;
constexpr int kMapHeight     = 64;
constexpr const char* kTitle             = "Project L";
constexpr const char* kTerrainVertPath   = "shaders/terrain.vert";
constexpr const char* kTerrainFragPath   = "shaders/terrain.frag";
constexpr const char* kWireframeVertPath = "shaders/wireframe.vert";
constexpr const char* kWireframeFragPath = "shaders/wireframe.frag";
constexpr const char* kObstacleVertPath  = "shaders/obstacle.vert";
constexpr const char* kObstacleFragPath  = "shaders/obstacle.frag";
constexpr const char* kSkinnedVertPath   = "shaders/skinned.vert";
constexpr const char* kSkinnedFragPath   = "shaders/skinned.frag";
constexpr const char* kOutlineVertPath        = "shaders/outline.vert";
constexpr const char* kOutlineFragPath        = "shaders/outline.frag";
constexpr const char* kOutlineMaskVertPath    = "shaders/outline_mask.vert";
constexpr const char* kOutlineMaskFragPath    = "shaders/outline_mask.frag";
constexpr const char* kOutlineCompositeVertPath = "shaders/outline_composite.vert";
constexpr const char* kOutlineCompositeFragPath = "shaders/outline_composite.frag";
constexpr const char* kShadowInstVertPath    = "shaders/shadow_instanced.vert";
constexpr const char* kShadowSkinnedVertPath = "shaders/shadow_skinned.vert";
constexpr const char* kShadowFragPath        = "shaders/shadow.frag";
constexpr const char* kPlayerModelPath   = "assets/models/player.glb";
constexpr const char* kTreeModelPath     = "assets/models/tree.gltf";
constexpr const char* kWaterVertPath     = "shaders/water.vert";
constexpr const char* kWaterFragPath     = "shaders/water.frag";
constexpr const char* kWaterNormalPath   = "assets/water_normal.png";
constexpr const char* kWorldMapPath      = "worldMap.json";
constexpr int         kShadowMapSize     = 2048;

constexpr glm::vec3 kPlayerColor  { 0.62f, 0.45f, 0.30f};  // skin tone, modulated by Lambert
constexpr float     kPlayerScale  = 1.0f;

// Convert sun (yaw, pitch) in degrees to a unit "light travel" vector
// (sun-toward-ground). yaw is around +Y measured from +Z toward +X; pitch
// is the downward tilt in degrees (0 = at horizon, 90 = straight down).
glm::vec3 sunDirectionFromYawPitch(float yawDeg, float pitchDeg) {
  const float yaw   = glm::radians(yawDeg);
  const float pitch = glm::radians(pitchDeg);
  const float c = std::cos(pitch);
  return {
    std::sin(yaw) * c,
    -std::sin(pitch),
    std::cos(yaw) * c,
  };
}

// Avg of 4 corner heights at the integer tile (tx, ty).
float tileWorldY(const shared::WorldMapFile& map, int tx, int ty) {
  const int W = map.width;
  const int H = map.height;
  if (W <= 0 || H <= 0 || tx < 0 || ty < 0 || tx >= W || ty >= H) return 0.0f;
  const auto& vh = map.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return 0.0f;
  const float SW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float SE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  const float NW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float NE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}

// Standard slab-method ray-vs-AABB test.
// Returns the t at which the ray first enters the box, or -1 if no hit.
// Rays that start inside the box return the exit t (also positive).
float rayVsAABB(glm::vec3 ro, glm::vec3 rd, glm::vec3 bMin, glm::vec3 bMax) {
  float tMin = 1e-4f, tMax = FLT_MAX;
  for (int i = 0; i < 3; ++i) {
    if (std::abs(rd[i]) < 1e-8f) {
      if (ro[i] < bMin[i] || ro[i] > bMax[i]) return -1.0f;
      continue;
    }
    const float invD = 1.0f / rd[i];
    float t0 = (bMin[i] - ro[i]) * invD;
    float t1 = (bMax[i] - ro[i]) * invD;
    if (t0 > t1) std::swap(t0, t1);
    tMin = std::max(tMin, t0);
    tMax = std::min(tMax, t1);
    if (tMax < tMin) return -1.0f;
  }
  return tMin;
}

std::filesystem::path resolveFromExe(const char* relative) {
  wchar_t buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n == MAX_PATH) return std::filesystem::path(relative);
  return std::filesystem::path(buf).parent_path() / relative;
}

// Returns the world position the camera should track. Falls back to the map
// center when no player position is known yet.
glm::vec3 followTargetForMap(int w, int h) {
  return { static_cast<float>(w) * 0.5f, 0.0f, static_cast<float>(h) * 0.5f };
}

// Server's PlayerState.facing -> Y-axis rotation in radians.
// Camera convention: south = 0, east = +π/2, north = π, west = -π/2.
// Diagonal directions sit at the 45-degree intercardinal angles.
float facingToYaw(const std::string& facing) {
  if (facing == "north")      return  3.14159265f;   // π
  if (facing == "north_east") return  2.35619449f;   // 3π/4
  if (facing == "east")       return  1.57079632f;   // π/2
  if (facing == "south_east") return  0.78539816f;   // π/4
  if (facing == "south")      return  0.0f;
  if (facing == "south_west") return -0.78539816f;   // -π/4
  if (facing == "west")       return -1.57079632f;   // -π/2
  if (facing == "north_west") return -2.35619449f;   // -3π/4
  return 0.0f;
}

// Returns true for movement clips that should be suppressed while turning.
static bool isMovementClip(const char* name) {
  return name == std::string_view("Walk_Loop") || name == std::string_view("Sprint_Loop");
}

// Smallest signed angle between two yaw values (result in [-π, π]).
static float yawDelta(float from, float to) {
  constexpr float kTwoPi = 6.28318530f;
  constexpr float kPi    = 3.14159265f;
  return std::fmod(to - from + kTwoPi + kPi, kTwoPi) - kPi;
}

// Choose the base animation clip for a player's current movement state.
// `prev` is the player state from the previous server tick (may be nullptr).
// One-shot overrides (attack, hit, pickup) are applied by the caller on top.
//
// The path is consumed server-side before broadcast, so a single-tile move
// arrives with path=[] and tileX/Y already at the destination.  We detect
// that case by comparing curr vs prev positions rather than path length.
const char* clipForPlayer(const shared::PlayerState* p,
                          const shared::PlayerState* prev = nullptr) {
  if (!p) return "Idle_Loop";
  if (p->dying) return "Death01";

  const bool hasPending = !p->path.empty();
  const bool justMoved  = prev &&
                          (prev->tileX != p->tileX || prev->tileY != p->tileY);

  if (!hasPending && !justMoved) return "Idle_Loop";

  // Helper: was the movement step diagonal?
  auto isDiag = [](int dx, int dy) { return dx != 0 && dy != 0; };

  if (hasPending) {
    // More than one step remaining → Sprint.
    if (p->path.size() > 1) return "Sprint_Loop";
    // Exactly one step left: cardinal → Walk, diagonal → Sprint.
    const int dx = p->path[0].x - p->tileX;
    const int dy = p->path[0].y - p->tileY;
    return isDiag(dx, dy) ? "Sprint_Loop" : "Walk_Loop";
  }

  // No pending path but we moved this tick — drive from the delta.
  const int dx = p->tileX - prev->tileX;
  const int dy = p->tileY - prev->tileY;
  return isDiag(dx, dy) ? "Sprint_Loop" : "Walk_Loop";
}
}  // namespace

App::~App() {
  spriteCache_.destroy();
  if (imguiInited_) shutdownImGui();
  destroyHoverMesh();
  if (outlineMaskFbo_) glDeleteFramebuffers(1, &outlineMaskFbo_);
  if (outlineMaskTex_) glDeleteTextures(1, &outlineMaskTex_);
  if (outlineQuadVao_) glDeleteVertexArrays(1, &outlineQuadVao_);
}

bool App::init() {
  if (!window_.init(kInitialWidth, kInitialHeight, kTitle)) return false;

  render::installGlDebugCallback();

  msaa_ = std::make_unique<render::MsaaFramebuffer>(
      window_.framebufferWidth(), window_.framebufferHeight(), kMsaaSamples);

  window_.onFramebufferResize = [this](int w, int h) { onResize(w, h); };
  // ImGui's GLFW backend chains these — its handlers run first, then ours.
  // We bail when ImGui claims the mouse so clicks on UI don't rotate the
  // camera and scroll over a slider zooms it instead of the world.
  // Also bail when a Clay UI element owns the pointer (s_clayOwned from prev frame).
  window_.onMouseButton = [this](int button, int action, int mods) {
    // Always forward RELEASE to the camera so that releasing middle-mouse
    // while the cursor is over UI can't leave the drag/rotation stuck.
    if (action == GLFW_RELEASE)
      camera_.onMouseButton(button, action);

    // ── Minimap click-to-walk (intercept before standard UI guard) ──────────────
    // clayMinimapHovered() returns the previous frame's hover state (one-frame
    // delay is imperceptible).  We still require Clay UI to be on and the player
    // to be connected so we can send the action.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS
        && showClayUi_ && ui::clayMinimapHovered()
        && currLocalPlayer_
        && network_.status() == net::Connection::Connected
        && !ui::ctxMenu().open) {
      double cx, cy;
      glfwGetCursorPos(window_.handle(), &cx, &cy);
      const float rad     = static_cast<float>(ui::MinimapRenderer::kSize) * 0.5f;
      // Minimap center: top-right, kMmMargin=24px margin (must match ClayHudPanel).
      const float fw2     = static_cast<float>(window_.framebufferWidth());
      const float centerX = fw2 - 24.f - rad;
      const float centerY = 24.f + rad;
      const float normX   = (static_cast<float>(cx) - centerX) / rad;
      const float normY   = (static_cast<float>(cy) - centerY) / rad;
      if (normX * normX + normY * normY <= 1.0f) {
        // Undo minimap rotation + lookAtLH X-flip: east=left so negate normX first.
        const float yaw = camera_.cameraYaw();
        const float fnx = -normX;  // flip to tile-space X (east = positive)
        const float ux  = (std::cos(yaw) * fnx + std::sin(yaw) * normY) * minimapTileRadius_;
        const float uy  = (-std::sin(yaw) * fnx + std::cos(yaw) * normY) * minimapTileRadius_;
        const int tx = std::clamp(static_cast<int>(std::round(
                           static_cast<float>(currLocalPlayer_->tileX) + ux)),
                           0, map_.width - 1);
        const int ty = std::clamp(static_cast<int>(std::round(
                           static_cast<float>(currLocalPlayer_->tileY) + uy)),
                           0, map_.height - 1);
        // Send the clicked tile directly. The server owns pathfinding and routes
        // around any blocked tiles (obstacles, water, NPC spawns, etc.).
        network_.sendMoveTo(tx, ty);
        oneShotClip_.clear();
        ui::clickFeedbackSpawn(static_cast<float>(cx), static_cast<float>(cy), 0);
        clickFeedbackActive_ = true;
        clickFeedbackTime_   = std::chrono::steady_clock::now();
        clickFeedbackX_      = static_cast<float>(cx);
        clickFeedbackY_      = static_cast<float>(cy);
        clickFeedbackColor_  = 0;
      }
      return;  // consumed — don't fall through to world dispatch
    }

    if (ImGui::GetIO().WantCaptureMouse) return;
    if (showClayUi_ && ui::clayIsPointerOverUI()) return;
    (void)mods;
    // PRESS events only reach the camera when UI doesn't own the mouse.
    if (action == GLFW_PRESS)
      camera_.onMouseButton(button, action);

    // Left-click: defer world dispatch to renderFrame() so the guards
    // (WantCaptureMouse and clayIsPointerOverUI) are evaluated with current-frame
    // data rather than the previous frame's stale state.  onMouseButton fires
    // during pollEvents(), before ImGui::NewFrame() and clayFrame() update them.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
      double cx2, cy2;
      glfwGetCursorPos(window_.handle(), &cx2, &cy2);
      pendingWorldLeftClick_ = true;
      pendingWorldClickX_    = static_cast<float>(cx2);
      pendingWorldClickY_    = static_cast<float>(cy2);
    }
    // Right-click: context menu driven by hoveredEntity_ (AABB hit).
    // Entries are for the specific entity the cursor is over; "Walk here" is
    // always appended.  When no entity was hit but terrain was, only "Walk here"
    // is shown.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS
        && network_.status() == net::Connection::Connected) {
      const bool anyHit = (hoveredEntity_.kind != HoveredEntity::Kind::None)
                        || hoveredTile_.hit;
      ctxMenuRequest_ = true;
      ctxMenuTileHit_ = anyHit;
      // Latch tile coords: entity tile for dispatch, terrain tile as fallback.
      if (hoveredEntity_.kind != HoveredEntity::Kind::None) {
        ctxMenuTileX_ = hoveredEntity_.tileX;
        ctxMenuTileY_ = hoveredEntity_.tileY;
      } else if (hoveredTile_.hit) {
        ctxMenuTileX_ = hoveredTile_.tileX;
        ctxMenuTileY_ = hoveredTile_.tileY;
      }
      ctxMenuPlayerId_.clear();

      if (anyHit) {
        double mx2, my2;
        glfwGetCursorPos(window_.handle(), &mx2, &my2);
        int fw2, fh2;
        glfwGetFramebufferSize(window_.handle(), &fw2, &fh2);
        ui::CtxMenuState& cm = ui::ctxMenu();
        cm.open             = true;
        cm.x                = static_cast<float>(mx2);
        cm.y                = static_cast<float>(my2);
        cm.screenW          = static_cast<float>(fw2);
        cm.screenH          = static_cast<float>(fh2);
        cm.entries.clear();
        cm.clickedIndex     = -1;
        cm.inventoryCtxSlot = -1;
        cm.equipCtxSlot.clear();
        cm.contextItemId.clear();

        // Entity-specific entries (only for the AABB winner).
        if (hoveredEntity_.kind == HoveredEntity::Kind::Obstacle) {
          const int ex = hoveredEntity_.tileX, ey = hoveredEntity_.tileY;
          if (ey >= 0 && ey < static_cast<int>(map_.tiles.size()) &&
              ex >= 0 && ex < static_cast<int>(map_.tiles[ey].size())) {
            const auto obs = map_.tiles[ey][ex].obstacle;
            if      (obs == shared::ObstacleType::tree)
              cm.entries.push_back({ "Chop down", "Tree" });
            else if (obs == shared::ObstacleType::rock)
              cm.entries.push_back({ "Mine", "Rock" });
            else if (obs == shared::ObstacleType::chest)
              cm.entries.push_back({ "Bank", "Chest" });
          }
        } else if (hoveredEntity_.kind == HoveredEntity::Kind::Npc) {
          for (const auto& n : npcs_) {
            if (n.id != hoveredEntity_.id || n.dying) continue;
            std::string dname = ui::npcName(n.kind);
            if (ui::npcIsAttackable(n.kind)) cm.entries.push_back({ "Attack",  dname });
            else                             cm.entries.push_back({ "Talk-to", dname });
            cm.entries.push_back({ "Examine", dname });
            break;
          }
        } else if (hoveredEntity_.kind == HoveredEntity::Kind::RemotePlayer) {
          auto rpIt = currRemotePlayers_.find(hoveredEntity_.id);
          if (rpIt != currRemotePlayers_.end() && !rpIt->second.dying) {
            ctxMenuPlayerId_ = hoveredEntity_.id;
            const std::string& rpName = rpIt->second.playerName;
            cm.entries.push_back({ "Trade with", rpName });
            cm.entries.push_back({ "Follow",     rpName });
            cm.entries.push_back({ "Examine",    rpName });
          }
        } else if (hoveredEntity_.kind == HoveredEntity::Kind::DroppedItem) {
          // Show a "Take" entry for every item on this tile (not just the AABB
          // winner) so the player can pick up each individual stack.
          // payload stores the item's server id so dispatch is unambiguous even
          // when multiple items share the same display name.
          const int itx = hoveredEntity_.tileX, ity = hoveredEntity_.tileY;
          for (const auto& it : droppedItems_) {
            if (it.tileX != itx || it.tileY != ity) continue;
            cm.entries.push_back({ "Take", ui::itemName(it.itemId), it.id });
          }
        }
        // Walk here always appears at the bottom.
        cm.entries.push_back({ "Walk here", "" });
      }
    }
  };
  window_.onScroll = [this](double /*xoffset*/, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (ui::clayMinimapHovered()) return;  // minimap consumes scroll; don't zoom camera
    camera_.onScroll(yoffset);
  };
  window_.onKey = [this](int key, int action, int /*mods*/) {
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
      showClayDebug_ = !showClayDebug_;
      ui::claySetDebugMode(showClayDebug_);
    }
  };

  if (!terrainShader_.fromFiles(resolveFromExe(kTerrainVertPath),
                                resolveFromExe(kTerrainFragPath))) {
    std::fprintf(stderr, "[App] terrain shader load failed\n");
    return false;
  }
  if (!wireframeShader_.fromFiles(resolveFromExe(kWireframeVertPath),
                                  resolveFromExe(kWireframeFragPath))) {
    std::fprintf(stderr, "[App] wireframe shader load failed\n");
    return false;
  }
  if (!obstacleShader_.fromFiles(resolveFromExe(kObstacleVertPath),
                                 resolveFromExe(kObstacleFragPath))) {
    std::fprintf(stderr, "[App] obstacle shader load failed\n");
    return false;
  }
  if (!skinnedShader_.fromFiles(resolveFromExe(kSkinnedVertPath),
                                resolveFromExe(kSkinnedFragPath))) {
    std::fprintf(stderr, "[App] skinned shader load failed\n");
    return false;
  }
  if (!outlineShader_.fromFiles(resolveFromExe(kOutlineVertPath),
                                resolveFromExe(kOutlineFragPath))) {
    std::fprintf(stderr, "[App] outline shader load failed\n");
    return false;
  }
  if (!outlineMaskShader_.fromFiles(resolveFromExe(kOutlineMaskVertPath),
                                    resolveFromExe(kOutlineMaskFragPath))) {
    std::fprintf(stderr, "[App] outline mask shader load failed\n");
    return false;
  }
  if (!outlineCompositeShader_.fromFiles(resolveFromExe(kOutlineCompositeVertPath),
                                         resolveFromExe(kOutlineCompositeFragPath))) {
    std::fprintf(stderr, "[App] outline composite shader load failed\n");
    return false;
  }
  if (!shadowInstancedShader_.fromFiles(resolveFromExe(kShadowInstVertPath),
                                        resolveFromExe(kShadowFragPath))) {
    std::fprintf(stderr, "[App] shadow instanced shader load failed\n");
    return false;
  }
  if (!shadowSkinnedShader_.fromFiles(resolveFromExe(kShadowSkinnedVertPath),
                                      resolveFromExe(kShadowFragPath))) {
    std::fprintf(stderr, "[App] shadow skinned shader load failed\n");
    return false;
  }
  if (!shadowMap_.init(kShadowMapSize)) {
    std::fprintf(stderr, "[App] shadow map init failed\n");
    return false;
  }

  // Water renderer — non-fatal if shaders are missing
  if (!waterRenderer_.init(resolveFromExe(kWaterVertPath).string(),
                           resolveFromExe(kWaterFragPath).string(),
                           resolveFromExe(kWaterNormalPath).string())) {
    std::fprintf(stderr, "[App] water renderer init failed — water will not render\n");
  }

  obstacles_.initGL();
  if (!obstacles_.loadTreeModel(resolveFromExe(kTreeModelPath))) {
    std::fprintf(stderr, "[App] tree model load failed — using procedural trees\n");
  }
  entities_.initGL();

  // Fetch entity definitions from the DB API.
  // Populates display name registries and loads custom NPC models.
  // One-time synchronous call at startup (server is always localhost).
  {
    editor::EntityClient dbClient;
    // NPC definitions — names, attackable flag, custom models.
    try {
      const auto npcDefs = dbClient.getNPCs();
      for (const auto& def : npcDefs) {
        if (!def.name.empty())  ui::g_npcNames[def.id]     = def.name;
        ui::g_npcAttackable[def.id] = def.isAttackable;
        if (def.modelPath.empty()) continue;
        auto model = world::loadGlb(resolveFromExe(def.modelPath.c_str()));
        if (!model)
          model = world::loadGlb(resolveFromExe(("assets/" + def.modelPath).c_str()));
        if (model && !model->primitives.empty()) {
          entities_.loadNpcKindModel(def.id, model->primitives, model->materials);
          std::fprintf(stdout, "[App] Loaded custom model for NPC '%s' (%zu prims)\n",
                       def.id.c_str(), model->primitives.size());
        }
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[App] DB NPC fetch failed (server offline?): %s\n", e.what());
    }
    // Item definitions — names only.
    try {
      const auto itemDefs = dbClient.getItems();
      for (const auto& def : itemDefs)
        if (!def.name.empty()) ui::g_itemNames[def.id] = def.name;
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[App] DB item fetch failed (server offline?): %s\n", e.what());
    }
  }

  generateAndBuildTerrain();
  initHoverMesh();

  // Screen-space outline infrastructure.
  // Empty VAO for the fullscreen-triangle composite draw (no buffers needed —
  // the vertex shader uses gl_VertexID to generate positions directly).
  glCreateVertexArrays(1, &outlineQuadVao_);
  // Allocate the mask FBO at the initial window size.
  initOutlineMaskFbo(kInitialWidth, kInitialHeight);
  // Player skinned mesh — failure is non-fatal so we still run if the
  // asset is missing; the player just won't render.
  if (!playerModel_.load(resolveFromExe(kPlayerModelPath))) {
    std::fprintf(stderr, "[App] player glTF load failed — proceeding without a player model\n");
  } else {
    playerModel_.setClip("Idle_Loop");
  }

  // Snap the camera to the map center so the first frame isn't mid-lerp.
  camera_.snapTo(followTargetForMap(terrainTileW_, terrainTileH_));

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  glDisable(GL_CULL_FACE);

  initImGui();

  { int fw, fh; glfwGetFramebufferSize(window_.handle(), &fw, &fh); ui::clayInit(fw, fh); }
  spriteCache_.init();
  minimap_.init();

  if (!audio_.init()) {
    std::fprintf(stderr, "[App] audio init failed — proceeding without sound\n");
  }

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
      shadowDarkness_  = s.shadowDarkness;
      shadowBias_      = s.shadowBias;
      shadowHalfExtent_= s.shadowHalfExtent;
      palette_     = s.palette;
      paletteHues_ = s.paletteHues;
      paletteSats_ = s.paletteSats;
      paletteLums_ = s.paletteLums;
      outlineRadius_    = s.outlineRadius;
      outlineDepthBias_ = s.outlineDepthBias;
      outlineColor_   = {s.outlineColorR, s.outlineColorG, s.outlineColorB, s.outlineColorA};
      hoverTileColor_ = {s.hoverTileR,    s.hoverTileG,    s.hoverTileB,    s.hoverTileA};
    }
  }

  lastFrameTime_ = std::chrono::steady_clock::now();
  return true;
}

int App::run() {
  while (!window_.shouldClose()) {
    window_.pollEvents();
    renderFrame();
    window_.swapBuffers();
  }
  return 0;
}

void App::generateAndBuildTerrain() {
  // Try to load a saved map from worldMap.json (level editor output).
  // If found, it provides terrain, obstacles, water tiles, and spawn data.
  // If not found, fall back to procedural generation (no water).
  const auto mapPath = resolveFromExe(kWorldMapPath);
  if (!shared::loadWorldMap(mapPath, map_)) {
    map_ = world::generateMap(kMapWidth, kMapHeight, mapSeed_, noiseFreq_, noiseAmp_);
    std::fprintf(stdout, "[App] no worldMap.json found — using procedural map\n");
  } else {
    std::fprintf(stdout, "[App] loaded worldMap.json (%dx%d, %zu water tiles)\n",
                 map_.width, map_.height, map_.waterTiles.size());
  }

  const auto data = world::buildTerrainMesh(map_);
  terrainMesh_.upload(data.positions, data.colors,
                      data.triangleIndices, data.lineIndices,
                      data.normals);
  terrainTileW_   = data.width;
  terrainTileH_   = data.height;
  terrainIndexCt_ = static_cast<int>(data.triangleIndices.size());
  hoveredTile_    = {};  // hover stale after regenerate

  obstacles_.rebuildFromMap(map_);

  // Rebuild minimap base layer for the new map.
  minimap_.buildBaseLayer(map_);

  // Rebuild water mesh from loaded/generated map.
  if (waterRenderer_.valid())
    waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);

  std::fprintf(stdout, "[App] terrain mesh: %d x %d tiles, %zu verts, %zu tri-idx, %zu line-idx\n",
               data.width, data.height,
               data.positions.size() / 3,
               data.triangleIndices.size(),
               data.lineIndices.size());
}

void App::initHoverMesh() {
  destroyHoverMesh();
  glCreateVertexArrays(1, &hoverVao_);
  glCreateBuffers(1, &hoverVbo_);
  // Persistent dynamic storage — we'll rewrite the 4-vertex contents each
  // frame via glNamedBufferSubData.
  glNamedBufferStorage(hoverVbo_, sizeof(float) * 3 * 4, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glVertexArrayVertexBuffer(hoverVao_, 0, hoverVbo_, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(hoverVao_, 0);
  glVertexArrayAttribFormat(hoverVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(hoverVao_, 0, 0);
}

void App::destroyHoverMesh() {
  if (hoverVbo_) glDeleteBuffers(1, &hoverVbo_);
  if (hoverVao_) glDeleteVertexArrays(1, &hoverVao_);
  hoverVao_ = hoverVbo_ = 0;
}

void App::updateHoverMesh(int tx, int ty, int szX, int szY) {
  // 4 corners of a szX × szY tile region whose SW corner is (tx, ty).
  const int   W   = terrainTileW_;
  const int   H   = terrainTileH_;
  const auto& vh  = map_.vertexHeights;
  if (W <= 0 || H <= 0 || vh.empty()) return;

  const int tx2 = tx + szX - 1;
  const int ty2 = ty + szY - 1;

  const float hSW = vh[(H - ty)      * (W + 1) + tx]      * shared::kMaxTerrainH;
  const float hSE = vh[(H - ty)      * (W + 1) + tx2 + 1] * shared::kMaxTerrainH;
  const float hNW = vh[(H - ty2 - 1) * (W + 1) + tx]      * shared::kMaxTerrainH;
  const float hNE = vh[(H - ty2 - 1) * (W + 1) + tx2 + 1] * shared::kMaxTerrainH;

  constexpr float kHoverBias = 0.015f;
  const float verts[12] = {
      tx  - 0.5f, hSW + kHoverBias, ty  - 0.5f,
      tx2 + 0.5f, hSE + kHoverBias, ty  - 0.5f,
      tx2 + 0.5f, hNE + kHoverBias, ty2 + 0.5f,
      tx  - 0.5f, hNW + kHoverBias, ty2 + 0.5f,
  };
  glNamedBufferSubData(hoverVbo_, 0, sizeof(verts), verts);
}

void App::renderFrame() {
  const auto now = std::chrono::steady_clock::now();
  const float dt = std::chrono::duration<float>(now - lastFrameTime_).count();
  lastFrameTime_ = now;

  // ---- Camera input plumbing ------------------------------------------------
  // Cursor position is polled (GLFW has no scroll-equivalent state poll, so
  // onScroll uses callbacks; but cursor pos is cheaper to poll than to wire).
  double cursorX = 0.0, cursorY = 0.0;
  glfwGetCursorPos(window_.handle(), &cursorX, &cursorY);
  camera_.onCursorPos(cursorX, cursorY);
  // Camera follows the local player when we have one, otherwise the map
  // center. Y is the world elevation at that tile so the look-at doesn't
  // sink below tall terrain.
  glm::vec3 followTarget = followTargetForMap(terrainTileW_, terrainTileH_);
  if (currLocalPlayer_) {
    // Center camera on the player mesh center (roughly waist height ~1.0 units
    // above the ground) so the pivot isn't at floor level.
    followTarget = {
      static_cast<float>(currLocalPlayer_->tileX),
      tileWorldY(map_, currLocalPlayer_->tileX, currLocalPlayer_->tileY) + 1.0f,
      static_cast<float>(currLocalPlayer_->tileY)
    };
  }
  camera_.update(dt, window_.handle(), followTarget);

  const int   fbW    = window_.framebufferWidth();
  const int   fbH    = window_.framebufferHeight();
  const float aspect = (fbH > 0) ? static_cast<float>(fbW) / static_cast<float>(fbH) : 1.0f;
  const glm::mat4 viewProj = camera_.viewProjection(aspect);

  // ---- Hover pick (skip if ImGui or Clay UI owns the mouse) -------------------
  // NOTE: clayIsPointerOverUI() is last frame's state here (clayFrame hasn't run yet).
  // claySteals is refreshed after clayFrame() below so that context info, outlines,
  // and tooltips use the current frame's ownership rather than a stale value.
  bool claySteals = showClayUi_ && ui::clayIsPointerOverUI();
  if (!ImGui::GetIO().WantCaptureMouse && !claySteals && fbW > 0 && fbH > 0) {
    glm::vec3 rayOrigin, rayDir;
    input::screenToRay(cursorX, cursorY, fbW, fbH, viewProj, &rayOrigin, &rayDir);
    hoveredTile_ = input::pickTile(rayOrigin, rayDir, map_.vertexHeights,
                                   terrainTileW_, terrainTileH_);

    // ---- Obstacle + entity ray-pick (secondary pass) -------------------------
    // Tests the ray against geometry-derived AABBs (inflated ×1.2) for all
    // interactables.  hoveredTile_ (terrain) is NOT overridden — it always
    // stays as the raw ground tile.  This pass writes hoveredEntity_ which
    // drives outline, context info, tooltip, left-click, and right-click.
    // Entities behind the terrain surface are rejected (bestT initialised to
    // the terrain hit distance so only closer AABB hits are accepted).
    hoveredEntity_ = {};
    {
      float bestT = hoveredTile_.hit ? hoveredTile_.rayT : FLT_MAX;
      int   bestTx = -1, bestTy = -1;
      HoveredEntity::Kind bestKind = HoveredEntity::Kind::None;
      std::string         bestId;

      // Helper: inflate a model-space AABB ×scale from its centre, then
      // translate it to world space at (wx, wy, wz).
      auto worldAABB = [](glm::vec3 lMin, glm::vec3 lMax, float scale,
                          float wx, float wy, float wz,
                          glm::vec3& outMin, glm::vec3& outMax) {
        const glm::vec3 lCentre  = (lMin + lMax) * 0.5f;
        const glm::vec3 halfExt  = (lMax - lMin) * 0.5f * scale;
        const glm::vec3 wCentre  = glm::vec3(wx, wy, wz) + lCentre;
        outMin = wCentre - halfExt;
        outMax = wCentre + halfExt;
      };

      // ---- Obstacles (trees / rocks / chests) --------------------------------
      for (int oty = 0; oty < terrainTileH_; ++oty) {
        for (int otx = 0; otx < terrainTileW_; ++otx) {
          const auto obs = map_.tiles[oty][otx].obstacle;
          if (obs == shared::ObstacleType::none) continue;

          const float baseY = tileWorldY(map_, otx, oty);

          // Model-space AABB (centred on tile, base at Y=0).
          glm::vec3 lMin, lMax;
          if (obs == shared::ObstacleType::tree) {
            if (obstacles_.treeModelLoaded()) {
              lMin = obstacles_.treeGltfAABBMin();
              lMax = obstacles_.treeGltfAABBMax();
            } else {
              lMin = glm::vec3(-0.45f,  0.00f, -0.45f);
              lMax = glm::vec3( 0.45f,  1.60f,  0.45f);
            }
          } else if (obs == shared::ObstacleType::rock) {
            lMin = glm::vec3(-0.28f,  0.00f, -0.24f);
            lMax = glm::vec3( 0.28f,  0.36f,  0.24f);
          } else {  // chest
            lMin = glm::vec3(-0.28f,  0.00f, -0.28f);
            lMax = glm::vec3( 0.28f,  0.56f,  0.28f);
          }

          glm::vec3 wMin, wMax;
          // Obstacles: AABB is derived from actual mesh vertices so no inflation
          // needed (1.0 = exact bounds).  1.2× would push the tree box wider than
          // the tile and register hits on open ground beside the visible mesh.
          worldAABB(lMin, lMax, 1.0f,
                    static_cast<float>(otx), baseY, static_cast<float>(oty),
                    wMin, wMax);

          const float t = rayVsAABB(rayOrigin, rayDir, wMin, wMax);
          if (t > 0.0f && t < bestT) {
            bestT    = t;
            bestTx   = otx;
            bestTy   = oty;
            bestKind = HoveredEntity::Kind::Obstacle;
            bestId.clear();
          }
        }
      }

      // ---- NPCs --------------------------------------------------------------
      for (const auto& npc : npcs_) {
        if (npc.dying) continue;
        const float baseY = tileWorldY(map_, npc.tileX, npc.tileY);
        // Humanoid AABB: ±0.18 XZ, 0..1.0 Y (body + head).
        glm::vec3 wMin, wMax;
        worldAABB(glm::vec3(-0.18f, 0.0f, -0.18f),
                  glm::vec3( 0.18f, 1.0f,  0.18f), 1.2f,
                  static_cast<float>(npc.tileX), baseY,
                  static_cast<float>(npc.tileY), wMin, wMax);

        const float t = rayVsAABB(rayOrigin, rayDir, wMin, wMax);
        if (t > 0.0f && t < bestT) {
          bestT    = t;
          bestTx   = npc.tileX;
          bestTy   = npc.tileY;
          bestKind = HoveredEntity::Kind::Npc;
          bestId   = npc.id;
        }
      }

      // ---- Dropped items -----------------------------------------------------
      for (const auto& item : droppedItems_) {
        const float baseY = tileWorldY(map_, item.tileX, item.tileY);
        // Small flat cylinder approximated as AABB: ±0.20 XZ, 0..0.20 Y.
        glm::vec3 wMin, wMax;
        worldAABB(glm::vec3(-0.20f, 0.0f, -0.20f),
                  glm::vec3( 0.20f, 0.20f,  0.20f), 1.2f,
                  static_cast<float>(item.tileX), baseY,
                  static_cast<float>(item.tileY), wMin, wMax);

        const float t = rayVsAABB(rayOrigin, rayDir, wMin, wMax);
        if (t > 0.0f && t < bestT) {
          bestT    = t;
          bestTx   = item.tileX;
          bestTy   = item.tileY;
          bestKind = HoveredEntity::Kind::DroppedItem;
          bestId   = item.id;
        }
      }

      // ---- Remote players ----------------------------------------------------
      for (const auto& [rpId, rp] : currRemotePlayers_) {
        if (rp.dying) continue;
        const float baseY = tileWorldY(map_, rp.tileX, rp.tileY);
        // Same humanoid AABB as NPCs.
        glm::vec3 wMin, wMax;
        worldAABB(glm::vec3(-0.18f, 0.0f, -0.18f),
                  glm::vec3( 0.18f, 1.0f,  0.18f), 1.2f,
                  static_cast<float>(rp.tileX), baseY,
                  static_cast<float>(rp.tileY), wMin, wMax);

        const float t = rayVsAABB(rayOrigin, rayDir, wMin, wMax);
        if (t > 0.0f && t < bestT) {
          bestT    = t;
          bestTx   = rp.tileX;
          bestTy   = rp.tileY;
          bestKind = HoveredEntity::Kind::RemotePlayer;
          bestId   = rpId;
        }
      }

      // Commit to hoveredEntity_ (hoveredTile_ is NOT modified — terrain stays).
      if (bestKind != HoveredEntity::Kind::None) {
        hoveredEntity_.kind  = bestKind;
        hoveredEntity_.tileX = bestTx;
        hoveredEntity_.tileY = bestTy;
        hoveredEntity_.id    = bestId;
        hoveredEntity_.rayT  = bestT;
      }
      // Keep convenience alias in sync.
      hoveredPlayerId_ = (bestKind == HoveredEntity::Kind::RemotePlayer) ? bestId : "";
    }
  } else {
    hoveredTile_.hit = false;
    hoveredEntity_   = {};
    hoveredPlayerId_.clear();
  }
  if (hoveredTile_.hit)
    updateHoverMesh(hoveredTile_.tileX, hoveredTile_.tileY);

  const glm::vec3 sunDir = sunDirectionFromYawPitch(sunYawDeg_, sunPitchDeg_);

  // ---- Phase 6b — shadow depth pass -----------------------------------------
  // Renders obstacle instances into the shadow map depth buffer. Skipped
  // entirely when shadows are toggled off; the receiver shaders also clamp
  // to "fully lit" via u_shadowsEnabled so the sampler binding still needs
  // to point at a valid texture.
  const glm::vec3 mapCenter = followTargetForMap(terrainTileW_, terrainTileH_);
  const glm::mat4 lightVP   = render::ShadowMap::lightViewProj(
      sunDir, mapCenter, shadowHalfExtent_);
  if (shadowsEnabled_) {
    shadowMap_.beginPass();

    // Obstacles + NPCs (both use the instanced shadow shader / same VAO layout)
    shadowInstancedShader_.use();
    shadowInstancedShader_.setMat4("u_lightViewProj", lightVP);
    obstacles_.renderDepth(shadowInstancedShader_);
    entities_.renderDepth(shadowInstancedShader_);

    // Local player + remote players (skinned meshes)
    if (playerModel_.isLoaded()) {
      shadowSkinnedShader_.use();
      shadowSkinnedShader_.setMat4("u_lightViewProj", lightVP);

      // Local player
      if (currLocalPlayer_) {
        const auto shadowNow = std::chrono::steady_clock::now();
        const auto shadowDtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            shadowNow - lastTickTime_).count();
        const float shadowAlpha = std::clamp(
            static_cast<float>(shadowDtMs) / static_cast<float>(shared::kTickDurationMs),
            0.0f, 1.0f);
        float sfx = static_cast<float>(currLocalPlayer_->tileX);
        float sfy = static_cast<float>(currLocalPlayer_->tileY);
        float syWorld = tileWorldY(map_, currLocalPlayer_->tileX, currLocalPlayer_->tileY);
        if (prevLocalPlayer_) {
          sfx    = std::lerp(static_cast<float>(prevLocalPlayer_->tileX), sfx, shadowAlpha);
          sfy    = std::lerp(static_cast<float>(prevLocalPlayer_->tileY), sfy, shadowAlpha);
          syWorld= std::lerp(tileWorldY(map_, prevLocalPlayer_->tileX, prevLocalPlayer_->tileY),
                             syWorld, shadowAlpha);
        }
        glm::mat4 shadowMat = glm::translate(glm::mat4(1.0f), glm::vec3(sfx, syWorld, sfy));
        shadowMat = glm::rotate(shadowMat, smoothedPlayerYaw_, glm::vec3(0.0f, 1.0f, 0.0f));
        shadowMat = glm::scale(shadowMat, glm::vec3(kPlayerScale));
        playerModel_.render(shadowSkinnedShader_, shadowMat);
      }

      // Remote players — use per-player animation state (renderAs) so each
      // shadow matches that player's actual running/idle clip, not the local one.
      {
        const auto rpNow   = std::chrono::steady_clock::now();
        const auto rpDtMs  = std::chrono::duration_cast<std::chrono::milliseconds>(rpNow - lastTickTime_).count();
        const float rpAlpha= std::clamp(static_cast<float>(rpDtMs) /
                                        static_cast<float>(shared::kTickDurationMs), 0.0f, 1.0f);
        for (const auto& [rpId, rp] : currRemotePlayers_) {
          if (rp.dying) continue;
          float rfx     = static_cast<float>(rp.tileX);
          float rfy     = static_cast<float>(rp.tileY);
          float ryWorld = tileWorldY(map_, rp.tileX, rp.tileY);
          auto prevIt = prevRemotePlayers_.find(rpId);
          if (prevIt != prevRemotePlayers_.end()) {
            rfx     = std::lerp(static_cast<float>(prevIt->second.tileX), rfx, rpAlpha);
            rfy     = std::lerp(static_cast<float>(prevIt->second.tileY), rfy, rpAlpha);
            ryWorld = std::lerp(tileWorldY(map_, prevIt->second.tileX, prevIt->second.tileY),
                                ryWorld, rpAlpha);
          }
          auto animIt = remoteAnims_.find(rpId);
          const float rpYaw       = animIt != remoteAnims_.end() ? animIt->second.yaw       : 0.0f;
          const int   rpClipIdx   = animIt != remoteAnims_.end() ? animIt->second.clipIndex  : -1;
          const float rpClipTime  = animIt != remoteAnims_.end() ? animIt->second.clipTime   : 0.0f;
          glm::mat4 rpMat = glm::translate(glm::mat4(1.0f), glm::vec3(rfx, ryWorld, rfy));
          rpMat = glm::rotate(rpMat, rpYaw, glm::vec3(0.0f, 1.0f, 0.0f));
          rpMat = glm::scale(rpMat, glm::vec3(kPlayerScale));
          playerModel_.renderAs(shadowSkinnedShader_, rpMat, rpClipIdx, rpClipTime);
        }
      }
    }

    shadowMap_.endPass();
  }

  // ---- Main pass into MSAA framebuffer (rebind after the shadow pass) ------
  msaa_->bind();
  glClearColor(0.45f, 0.65f, 0.85f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  // Shadow texture lives on unit 1; main-pass shaders sample it via
  // u_shadowMap = 1.
  glBindTextureUnit(1, shadowMap_.depthTexture());

  terrainShader_.use();
  terrainShader_.setInt  ("u_shadowMap",       1);
  terrainShader_.setMat4 ("u_lightViewProj",   lightVP);
  terrainShader_.setFloat("u_shadowsEnabled",  shadowsEnabled_ ? 1.0f : 0.0f);
  terrainShader_.setFloat("u_shadowDarkness",  shadowDarkness_);
  terrainShader_.setFloat("u_shadowBias",      shadowBias_);
  terrainShader_.setMat4 ("u_viewProj", viewProj);
  terrainShader_.setVec3 ("u_paletteLevels",
                          glm::vec3(static_cast<float>(paletteHues_),
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

  // ---- Hover tile outline — drawn immediately after terrain, BEFORE obstacles
  //      and NPCs, so that taller geometry (trees, rocks, NPCs) naturally
  //      overwrites these pixels in color and depth.  This guarantees the tile
  //      square is occluded by objects at all camera distances without relying
  //      on depth-buffer precision for tiny world-space offsets.
  if (hoveredTile_.hit) {
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color",    hoverTileColor_);
    // depthMask ON — we want subsequent obstacle/NPC draws to overwrite us.
    glBindVertexArray(hoverVao_);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glBindVertexArray(0);
  }

  // ---- Obstacles (instanced) -------------------------------------------------
  obstacleShader_.use();
  obstacleShader_.setMat4 ("u_viewProj",        viewProj);
  obstacleShader_.setMat4 ("u_lightViewProj",   lightVP);
  obstacleShader_.setVec3 ("u_lightDir",        sunDir);
  obstacleShader_.setVec3 ("u_paletteLevels",
                           glm::vec3(static_cast<float>(paletteHues_),
                                     static_cast<float>(paletteSats_),
                                     static_cast<float>(paletteLums_)));
  obstacleShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
  obstacleShader_.setFloat("u_ambient",         ambient_);
  obstacleShader_.setFloat("u_diffuse",         diffuse_);
  obstacleShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
  obstacleShader_.setInt  ("u_shadowMap",        1);
  obstacleShader_.setFloat("u_shadowsEnabled",  shadowsEnabled_ ? 1.0f : 0.0f);
  obstacleShader_.setFloat("u_shadowDarkness",  shadowDarkness_);
  obstacleShader_.setFloat("u_shadowBias",      shadowBias_);
  obstacleShader_.setFloat("u_fogEnabled", fogEnabled_  ? 1.0f : 0.0f);
  obstacleShader_.setVec3 ("u_fogColor",   fogColor_);
  obstacleShader_.setFloat("u_fogDensity", fogDensity_);
  obstacleShader_.setFloat("u_fogStart",   fogStart_);
  obstacles_.render(obstacleShader_);

  // ---- Detect connection-status transitions for chat-log + state reset -----
  {
    const auto cur = network_.status();
    if (cur != lastNetStatus_) {
      if (cur == net::Connection::Disconnected || cur == net::Connection::Failed) {
        chatLog_.appendSystem("Disconnected from server. Use the Connect panel to reconnect.");
        ui::chatAppendSystem("Disconnected from server. Use the Connect panel to reconnect.");
        currLocalPlayer_.reset();
        prevLocalPlayer_.reset();
        prevNpcs_.clear();
        currNpcs_.clear();
        prevRemotePlayers_.clear();
        currRemotePlayers_.clear();
        remoteAnims_.clear();
        npcs_.clear();
        droppedItems_.clear();
        entities_.setNpcInstances({});
        entities_.setItemInstances({});
        loginAnnounced_   = false;
        smoothedYawValid_ = false;
      }
      lastNetStatus_ = cur;
    }
  }

  // ---- Network drain BEFORE entity rendering --------------------------------
  // We need fresh prev/curr NPC maps to produce smooth interpolation; with
  // the previous "drain after render" order each tick boundary popped.
  processNetworkMessages();

  // ---- NPCs + dropped items (Phase 5d + Phase 10 interp) --------------------
  // Reuses the obstacle shader (same uniforms already bound). Build a
  // per-frame interpolated instance array from prev/curr state by id.
  {
    const auto    dtMs  = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTickTime_).count();
    const float   alpha = std::clamp(static_cast<float>(dtMs) /
                                     static_cast<float>(shared::kTickDurationMs),
                                     0.0f, 1.0f);
    std::vector<world::EntityRenderer::Instance> insts;
    std::vector<std::string> kinds;
    insts.reserve(currNpcs_.size());
    kinds.reserve(currNpcs_.size());
    constexpr float kTwoPi = 6.28318531f;

    // Prune yaw entries for NPCs that are no longer in the current snapshot.
    for (auto it = npcSmoothedYaw_.begin(); it != npcSmoothedYaw_.end(); ) {
      if (currNpcs_.find(it->first) == currNpcs_.end()) it = npcSmoothedYaw_.erase(it);
      else ++it;
    }

    for (const auto& [id, curr] : currNpcs_) {
      if (curr.dying) continue;
      float fx = static_cast<float>(curr.tileX);
      float fy = static_cast<float>(curr.tileY);
      const float targetYaw = facingToYaw(curr.facing);
      auto pit = prevNpcs_.find(id);
      if (pit != prevNpcs_.end()) {
        fx = std::lerp(static_cast<float>(pit->second.tileX), fx, alpha);
        fy = std::lerp(static_cast<float>(pit->second.tileY), fy, alpha);
      }
      const float wy = tileWorldY(map_,
                                  static_cast<int>(std::round(fx)),
                                  static_cast<int>(std::round(fy)));

      // Smooth yaw — shortest-arc lerp, same half-life as the local player.
      auto yawIt = npcSmoothedYaw_.find(id);
      if (yawIt == npcSmoothedYaw_.end()) {
        npcSmoothedYaw_[id] = targetYaw;  // first sight: snap
        yawIt = npcSmoothedYaw_.find(id);
      } else {
        float& smoothYaw = yawIt->second;
        float delta = std::fmod(targetYaw - smoothYaw + kTwoPi + 3.14159265f,
                                kTwoPi) - 3.14159265f;
        const float k = 1.0f - std::exp(-dt / 0.08f);
        smoothYaw += delta * k;
      }
      const float smoothYaw = yawIt->second;

      insts.push_back({ fx, wy, fy, smoothYaw });  // NOLINT: shadowed const below is intentional
      kinds.push_back(curr.kind);
    }
    entities_.setNpcInstances(insts, kinds);
  }
  entities_.render(obstacleShader_);

  // ---- Local player (Phase 5: skinned glTF) ----------------------------------
  renderPlayer(viewProj, dt);

  // ---- Remote players — render each with independent animation & interpolation
  if (playerModel_.isLoaded() && network_.status() == net::Connection::Connected) {
    // Compute tick alpha for remote player interpolation (same basis as NPCs).
    const auto  nowRp   = std::chrono::steady_clock::now();
    const auto  dtMsRp  = std::chrono::duration_cast<std::chrono::milliseconds>(nowRp - lastTickTime_).count();
    const float rpAlpha = std::clamp(static_cast<float>(dtMsRp) /
                                     static_cast<float>(shared::kTickDurationMs),
                                     0.0f, 1.0f);

    skinnedShader_.use();
    skinnedShader_.setMat4 ("u_viewProj",       viewProj);
    skinnedShader_.setMat4 ("u_lightViewProj",  lightVP);
    skinnedShader_.setVec3 ("u_lightDir",       sunDir);
    skinnedShader_.setVec3 ("u_paletteLevels",
                            glm::vec3(static_cast<float>(paletteHues_),
                                      static_cast<float>(paletteSats_),
                                      static_cast<float>(paletteLums_)));
    skinnedShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
    skinnedShader_.setFloat("u_ambient",         ambient_);
    skinnedShader_.setFloat("u_diffuse",         diffuse_);
    skinnedShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
    skinnedShader_.setInt  ("u_shadowMap",       1);
    skinnedShader_.setFloat("u_shadowsEnabled",  shadowsEnabled_ ? 1.0f : 0.0f);
    skinnedShader_.setFloat("u_shadowDarkness",  shadowDarkness_);
    skinnedShader_.setFloat("u_shadowBias",      shadowBias_);
    skinnedShader_.setFloat("u_fogEnabled",  fogEnabled_  ? 1.0f : 0.0f);
    skinnedShader_.setVec3 ("u_fogColor",    fogColor_);
    skinnedShader_.setFloat("u_fogDensity",  fogDensity_);
    skinnedShader_.setFloat("u_fogStart",    fogStart_);
    constexpr glm::vec3 kRemoteColor{0.50f, 0.38f, 0.28f};
    skinnedShader_.setVec3 ("u_color", kRemoteColor);

    for (const auto& [id, rp] : currRemotePlayers_) {
      if (rp.dying) continue;

      // --- Position interpolation ---
      float fx = static_cast<float>(rp.tileX);
      float fy = static_cast<float>(rp.tileY);
      auto prevIt = prevRemotePlayers_.find(id);
      if (prevIt != prevRemotePlayers_.end()) {
        fx = std::lerp(static_cast<float>(prevIt->second.tileX), fx, rpAlpha);
        fy = std::lerp(static_cast<float>(prevIt->second.tileY), fy, rpAlpha);
      }
      const float rpWorldY = tileWorldY(map_,
                                        static_cast<int>(std::round(fx)),
                                        static_cast<int>(std::round(fy)));

      // --- Yaw smoothing ---
      const float targetYaw = facingToYaw(rp.facing);
      auto& ra = remoteAnims_[id];
      constexpr float kTwoPi = 6.28318531f;
      float delta = std::fmod(targetYaw - ra.yaw + kTwoPi + 3.14159265f,
                              kTwoPi) - 3.14159265f;
      const float yawK = 1.0f - std::exp(-dt / 0.08f);
      ra.yaw += delta * yawK;

      // --- Independent animation (one-shot override mirrors local player) ---
      const auto nowAnim = std::chrono::steady_clock::now();
      const char* desiredClip;
      if (!ra.oneShotClip.empty() && nowAnim < ra.oneShotEndsAt) {
        desiredClip = ra.oneShotClip.c_str();
      } else {
        if (!ra.oneShotClip.empty()) ra.oneShotClip.clear();
        auto prevRpIt = prevRemotePlayers_.find(id);
        const shared::PlayerState* prevRp =
          prevRpIt != prevRemotePlayers_.end() ? &prevRpIt->second : nullptr;
        desiredClip = clipForPlayer(&rp, prevRp);
        // Same turn-suppression as local player.
        constexpr float kTurnThreshold = 1.05f;
        if (isMovementClip(desiredClip) &&
            std::abs(yawDelta(ra.yaw, targetYaw)) > kTurnThreshold) {
          desiredClip = "Idle_Loop";
        }
      }
      const int wantIdx = playerModel_.findClipIndex(desiredClip);
      if (wantIdx != ra.clipIndex) {
        ra.clipIndex = wantIdx;
        ra.clipTime  = 0.0f;
      }
      ra.clipTime += dt;

      glm::mat4 rpModel = glm::translate(glm::mat4(1.0f), glm::vec3(fx, rpWorldY, fy));
      rpModel = glm::rotate(rpModel, ra.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
      rpModel = glm::scale(rpModel, glm::vec3(kPlayerScale));
      playerModel_.renderAs(skinnedShader_, rpModel, ra.clipIndex, ra.clipTime);
    }

    // Prune remoteAnims_ entries for players that have left.
    for (auto it = remoteAnims_.begin(); it != remoteAnims_.end(); ) {
      if (currRemotePlayers_.find(it->first) == currRemotePlayers_.end())
        it = remoteAnims_.erase(it);
      else
        ++it;
    }
  }

  // ---- SSR snapshot — resolve BEFORE outlines so water doesn't reflect
  //      tile/entity outlines (they're decorative UI, not world geometry).
  if (!map_.waterTiles.empty() && waterRenderer_.valid()) {
    msaa_->resolve();
    msaa_->resolveDepth();
    msaa_->bind();
    glViewport(0, 0, fbW, fbH);
  }

  // ---- Water pass ------------------------------------------------------------
  // Drawn here — after opaque geometry and SSR snapshot but BEFORE the hover
  // tile outline and entity stencil outlines, so those always composite on top
  // of water rather than being hidden beneath it.
  if (!map_.waterTiles.empty() && waterRenderer_.valid()) {
    waterRenderer_.render(
        static_cast<float>(glfwGetTime()),
        viewProj,
        msaa_->resolveColorTexture(),
        msaa_->resolveDepthTexture(),
        waterUniforms_);
  }

  // ---- Wireframe grid overlay ------------------------------------------------
  if (wireframe_) {
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color",    glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    glDepthMask(GL_FALSE);
    terrainMesh_.drawLines();
    glDepthMask(GL_TRUE);
  }

  // ---- Screen-space outline for hovered interactables ----------------------
  // Two-pass approach:
  //   Pass A — Render the hovered entity's geometry flat-white into the R8
  //             mask FBO. Manual depth comparison against the pre-resolved
  //             scene depth texture discards occluded fragments, giving a
  //             clean silhouette of whatever is actually visible.
  //   Pass B — Fullscreen composite: sample the mask at 16 ring offsets;
  //             pixels where the ring is filled but the center is empty are
  //             border pixels — alpha-blend those in the outline color.
  //
  // Unlike world-space normal inflation this produces gap-free, constant-
  // pixel-width outlines regardless of mesh topology, face normals, or the
  // angle between adjacent hard-edge faces.
  // Outline is driven by hoveredEntity_ (mesh AABB hit), not hoveredTile_ (terrain).
  if (hoveredEntity_.kind != HoveredEntity::Kind::None
      && network_.status() == net::Connection::Connected
      && outlineMaskFbo_ && outlineMaskTex_ && msaa_->resolveDepthTexture()) {
    const int htx = hoveredEntity_.tileX;
    const int hty = hoveredEntity_.tileY;

    // Resolve what geometry to render into the mask based on entity kind.
    bool hasObstacle = (hoveredEntity_.kind == HoveredEntity::Kind::Obstacle);
    bool hasNpc      = false;
    bool hasItem     = false;
    world::EntityRenderer::Instance npcInst{}, itemInst{};
    std::string hoveredNpcKind;

    if (hoveredEntity_.kind == HoveredEntity::Kind::Npc) {
      // Match against the interpolated CPU-side instance list so the outline
      // uses the exact same position and smoothed yaw as the rendered mesh.
      const auto& cpuInsts = entities_.npcInstsCpu();
      const auto& cpuKinds = entities_.npcKindsCpu();
      for (std::size_t i = 0; i < cpuInsts.size(); ++i) {
        const auto& inst = cpuInsts[i];
        const int itx = static_cast<int>(std::round(inst.x));
        const int ity = static_cast<int>(std::round(inst.z));
        if (itx == htx && ity == hty) {
          hasNpc         = true;
          npcInst        = inst;  // includes live rotY
          hoveredNpcKind = (i < cpuKinds.size()) ? cpuKinds[i] : "";
          break;
        }
      }
      // Fallback if interpolated position hasn't landed exactly on the tile yet.
      if (!hasNpc) {
        hasNpc = true;
        npcInst = { static_cast<float>(htx), tileWorldY(map_, htx, hty),
                    static_cast<float>(hty), 0.0f };
        for (const auto& n : npcs_)
          if (n.id == hoveredEntity_.id) { hoveredNpcKind = n.kind; break; }
      }
    } else if (hoveredEntity_.kind == HoveredEntity::Kind::DroppedItem) {
      for (const auto& di : droppedItems_) {
        if (di.id == hoveredEntity_.id) {
          hasItem  = true;
          itemInst = { static_cast<float>(di.tileX),
                       tileWorldY(map_, di.tileX, di.tileY),
                       static_cast<float>(di.tileY), 0.0f };
          break;
        }
      }
    }
    // RemotePlayer — no outline rendered yet.

    if (hasObstacle || hasNpc || hasItem) {
      // ── Pass A: render silhouette into mask FBO ──────────────────────────
      glBindFramebuffer(GL_FRAMEBUFFER, outlineMaskFbo_);
      const GLfloat kBlack[] = {0.0f, 0.0f, 0.0f, 0.0f};
      glClearNamedFramebufferfv(outlineMaskFbo_, GL_COLOR, 0, kBlack);
      glViewport(0, 0, fbW, fbH);

      glDisable(GL_STENCIL_TEST);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);   // read-only depth via manual comparison in shader
      glDepthFunc(GL_ALWAYS);  // let the fragment shader decide via texture lookup

      outlineMaskShader_.use();
      outlineMaskShader_.setMat4("u_viewProj",   viewProj);
      outlineMaskShader_.setInt ("u_sceneDepth",  2);  // texture unit 2
      outlineMaskShader_.setVec2("u_screenSize",  glm::vec2(static_cast<float>(fbW),
                                                             static_cast<float>(fbH)));
      outlineMaskShader_.setFloat("u_depthBias",  outlineDepthBias_);
      glBindTextureUnit(2, msaa_->resolveDepthTexture());

      if (hasObstacle) obstacles_.renderGeometryAt(outlineMaskShader_, map_, htx, hty);
      if (hasNpc)      entities_.renderNpcGeometry (outlineMaskShader_, npcInst, hoveredNpcKind);
      if (hasItem)     entities_.renderItemGeometry(outlineMaskShader_, itemInst);

      glDepthMask(GL_TRUE);
      glDepthFunc(GL_LESS);

      // ── Pass B: composite outline border over MSAA scene ─────────────────
      msaa_->bind();
      glViewport(0, 0, fbW, fbH);

      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      outlineCompositeShader_.use();
      outlineCompositeShader_.setInt  ("u_mask",          3);  // texture unit 3
      outlineCompositeShader_.setVec2 ("u_pixelSize",     glm::vec2(1.0f / fbW,
                                                                    1.0f / fbH));
      outlineCompositeShader_.setFloat("u_outlineRadius", outlineRadius_);
      outlineCompositeShader_.setVec4 ("u_outlineColor",  outlineColor_);
      glBindTextureUnit(3, outlineMaskTex_);

      glBindVertexArray(outlineQuadVao_);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);

      glDisable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);
    }
  }

  // ---- Resolve to single-sample + blit to window ----------------------------
  msaa_->resolve();
  msaa_->blitToDefault(fbW, fbH);

  // ---- ImGui UI pass on default framebuffer ----------------------------------
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ---- Clay UI pass ----------------------------------------------------------
  if (showClayUi_) {
    ImVec2 mp      = ImGui::GetMousePos();
    bool   md      = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool   lClick  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool   rClick  = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const shared::PlayerState* localPlayer =
        currLocalPlayer_ ? &currLocalPlayer_.value() : nullptr;

    // Pre-compute context info verb/subject for this frame.
    // Must be stable until after clayFrame returns.
    static std::string s_ctxVerbStr, s_ctxSubjStr;
    const char* ctxVerb    = "";
    const char* ctxSubject = "";
    const bool  uiOwned2   = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    if (hoveredEntity_.kind != HoveredEntity::Kind::None && !uiOwned2 && !claySteals) {
      // Entity AABB hit — derive verb/subject directly from the winning entity.
      switch (hoveredEntity_.kind) {
        case HoveredEntity::Kind::Obstacle: {
          const int ex = hoveredEntity_.tileX, ey = hoveredEntity_.tileY;
          if (ey >= 0 && ey < static_cast<int>(map_.tiles.size()) &&
              ex >= 0 && ex < static_cast<int>(map_.tiles[ey].size())) {
            const auto obs = map_.tiles[ey][ex].obstacle;
            if      (obs == shared::ObstacleType::tree)  { ctxVerb = "Chop"; ctxSubject = "Tree"; }
            else if (obs == shared::ObstacleType::rock)  { ctxVerb = "Mine"; ctxSubject = "Rock"; }
            else if (obs == shared::ObstacleType::chest) { ctxVerb = "Bank"; ctxSubject = "Chest"; }
          }
          break;
        }
        case HoveredEntity::Kind::Npc:
          for (const auto& n : npcs_) {
            if (n.id == hoveredEntity_.id && !n.dying) {
              ctxVerb      = ui::npcIsAttackable(n.kind) ? "Attack" : "Talk-to";
              s_ctxSubjStr = ui::npcName(n.kind);
              ctxSubject   = s_ctxSubjStr.c_str();
              break;
            }
          }
          break;
        case HoveredEntity::Kind::RemotePlayer: {
          auto rpIt = currRemotePlayers_.find(hoveredEntity_.id);
          if (rpIt != currRemotePlayers_.end() && !rpIt->second.dying) {
            ctxVerb      = "";
            s_ctxSubjStr = rpIt->second.playerName;
            ctxSubject   = s_ctxSubjStr.c_str();
          }
          break;
        }
        case HoveredEntity::Kind::DroppedItem:
          for (const auto& it : droppedItems_) {
            if (it.id == hoveredEntity_.id) {
              ctxVerb      = "Take";
              s_ctxSubjStr = ui::itemName(it.itemId);
              ctxSubject   = s_ctxSubjStr.c_str();
              break;
            }
          }
          break;
        default: break;
      }
    } else if (hoveredTile_.hit && !uiOwned2 && !claySteals) {
      // Bare terrain — show "Walk here" with no subject.
      ctxVerb    = "Walk here";
      ctxSubject = "";
    } else if (uiHover_.kind != ui::UiHoverState::Kind::None) {
      switch (uiHover_.kind) {
        case ui::UiHoverState::Kind::InventoryItem:
          s_ctxVerbStr = uiHover_.verb;
          ctxVerb      = s_ctxVerbStr.c_str();
          s_ctxSubjStr = uiHover_.itemName;
          ctxSubject   = s_ctxSubjStr.c_str();
          break;
        case ui::UiHoverState::Kind::EquipSlot:
          ctxVerb      = "Remove";
          s_ctxSubjStr = uiHover_.itemName;
          ctxSubject   = s_ctxSubjStr.c_str();
          break;
        case ui::UiHoverState::Kind::EmptyEquipSlot:
          s_ctxSubjStr = uiHover_.slotLabel;
          ctxSubject   = s_ctxSubjStr.c_str();
          break;
        default: break;
      }
    }

    // ── Build tooltip for this frame ─────────────────────────────────────────
    // UI hover uses PREVIOUS frame's uiHover_ (consistent with PointerOver 1-frame delay).
    // World hover uses current frame's hoveredTile_.
    // showTooltip() must be called BEFORE clayFrame (which calls buildTooltip).
    {
      using TL = ui::TooltipLine;
      using TS = ui::TooltipSeg;
      using TC = ui::TipColor;

      // UI hover tooltip (prev frame uiHover_)
      if (uiHover_.kind == ui::UiHoverState::Kind::SkillCard &&
          !uiHover_.tooltipLines.empty()) {
        ui::showTooltip(uiHover_.tooltipLines);
      } else if (uiHover_.kind == ui::UiHoverState::Kind::InventoryItem) {
        if (!uiHover_.verb.empty()) {
          ui::showTooltip({ TL{ {uiHover_.verb + " ", TC::White()},
                                {uiHover_.itemName,   TC::Orange()} } });
        } else {
          ui::showTooltip({ TL{ {uiHover_.itemName, TC::Orange()} } });
        }
      } else if (uiHover_.kind == ui::UiHoverState::Kind::EquipSlot) {
        ui::showTooltip({ TL{ {"Remove ", TC::White()},
                              {uiHover_.itemName, TC::Orange()} } });
      } else if (uiHover_.kind == ui::UiHoverState::Kind::EmptyEquipSlot &&
                 !uiHover_.slotLabel.empty()) {
        ui::showTooltip({ TL{ {uiHover_.slotLabel + " slot", TC::White()} } });
      } else if (hoveredEntity_.kind != HoveredEntity::Kind::None && !uiOwned2 && !claySteals) {
        // Entity AABB hit — tooltip matches context info verb/subject.
        switch (hoveredEntity_.kind) {
          case HoveredEntity::Kind::Obstacle: {
            const int ex = hoveredEntity_.tileX, ey = hoveredEntity_.tileY;
            if (ey >= 0 && ey < static_cast<int>(map_.tiles.size()) &&
                ex >= 0 && ex < static_cast<int>(map_.tiles[ey].size())) {
              const auto obs = map_.tiles[ey][ex].obstacle;
              if      (obs == shared::ObstacleType::tree)
                ui::showTooltip({ TL{ {"Chop ", TC::White()}, {"Tree",  TC::Orange()} } });
              else if (obs == shared::ObstacleType::rock)
                ui::showTooltip({ TL{ {"Mine ", TC::White()}, {"Rock",  TC::Orange()} } });
              else if (obs == shared::ObstacleType::chest)
                ui::showTooltip({ TL{ {"Bank ", TC::White()}, {"Chest", TC::Orange()} } });
            }
            break;
          }
          case HoveredEntity::Kind::Npc:
            for (const auto& n : npcs_) {
              if (n.id == hoveredEntity_.id && !n.dying) {
                const char* v = ui::npcIsAttackable(n.kind) ? "Attack" : "Talk-to";
                ui::showTooltip({ TL{ {std::string(v) + " ", TC::White()},
                                      {ui::npcName(n.kind),  TC::Orange()} } });
                break;
              }
            }
            break;
          case HoveredEntity::Kind::RemotePlayer: {
            auto rpIt = currRemotePlayers_.find(hoveredEntity_.id);
            if (rpIt != currRemotePlayers_.end() && !rpIt->second.dying) {
              const auto& rp = rpIt->second;
              int totalLevel = 0;
              for (const auto& [sid, sk] : rp.skills) totalLevel += sk.level;
              char lvlBuf[32];
              std::snprintf(lvlBuf, sizeof(lvlBuf), " (lvl-%d)", totalLevel);
              ui::showTooltip({ TL{ {rp.playerName,       TC::White()},
                                    {std::string(lvlBuf), TC::Gold()} } });
            }
            break;
          }
          case HoveredEntity::Kind::DroppedItem:
            for (const auto& di : droppedItems_) {
              if (di.id == hoveredEntity_.id) {
                ui::showTooltip({ TL{ {"Take ", TC::White()},
                                      {ui::itemName(di.itemId), TC::Orange()} } });
                break;
              }
            }
            break;
          default: break;
        }
      }
    }

    // ── Update minimap composite texture ────────────────────────────────────────
    if (showClayUi_ && minimap_.isReady()) {
      // Sub-tick interpolated player position for smooth map scrolling.
      float mmPx = localPlayer ? static_cast<float>(localPlayer->tileX) : 0.f;
      float mmPy = localPlayer ? static_cast<float>(localPlayer->tileY) : 0.f;
      if (localPlayer && prevLocalPlayer_) {
        const float mmAlpha = std::clamp(
            static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastTickTime_).count())
            / static_cast<float>(shared::kTickDurationMs),
            0.0f, 1.0f);
        mmPx = std::lerp(static_cast<float>(prevLocalPlayer_->tileX), mmPx, mmAlpha);
        mmPy = std::lerp(static_cast<float>(prevLocalPlayer_->tileY), mmPy, mmAlpha);
      }
      // Destination tile: show red triangle while player has an active path.
      int destX = -1, destY = -1;
      if (localPlayer && !localPlayer->path.empty()) {
        destX = localPlayer->destinationX;
        destY = localPlayer->destinationY;
      }
      minimap_.updateFrame(mmPx, mmPy, localPlayer, currRemotePlayers_, npcs_, droppedItems_,
                           camera_.cameraYaw(), minimapTileRadius_, destX, destY);
    }

    // Reset UI hover before Clay writes to it.
    uiHover_ = ui::UiHoverState{};
    const float wheelDelta = ImGui::GetIO().MouseWheel;
    const bool connected2  = (network_.status() == net::Connection::Connected);
    const bool showLogin   = !connected2;
    const bool showJoin    = connected2 && isNewPlayer_;
    ui::clayFrame(localPlayer, &network_, &spriteCache_, &uiHover_, dt, mp.x, mp.y,
                  static_cast<float>(fbW), static_cast<float>(fbH),
                  md, lClick, rClick, ctxVerb, ctxSubject, wheelDelta,
                  showLogin, showJoin, bankOpen_,
                  minimap_.isReady() ? minimap_.texture() : 0);

    // Refresh claySteals with the current frame's ownership (clayFrame() just ran
    // and updated s_clayOwned via Clay_PointerOver).  All rendering that reads
    // claySteals below (context info, tooltips, outline) now uses fresh data so
    // holding left-click while dragging over UI doesn't bleed hover through panels.
    claySteals = ui::clayIsPointerOverUI();
    if (claySteals) {
      hoveredEntity_ = {};
      hoveredTile_.hit = false;
    }

    // ── Minimap scroll zoom (handled after clayFrame so PointerOver is valid) ──
    if (wheelDelta != 0.f && ui::clayMinimapHovered()) {
      minimapTileRadius_ = std::max(5.f, std::min(24.f,
                                   minimapTileRadius_ - wheelDelta * 1.5f));
    }

    // North is indicated by the yellow notch in the minimap border ring (shader-rendered).

    // ── Deferred world left-click dispatch ──────────────────────────────────
    // clayFrame() above has now updated clayIsPointerOverUI() for the current
    // frame.  ImGui::NewFrame() has also run.  Both guards are current.
    if (pendingWorldLeftClick_) {
      pendingWorldLeftClick_ = false;
      const bool freshUiGuard = ImGui::GetIO().WantCaptureMouse
                              || (showClayUi_ && ui::clayIsPointerOverUI());
      if (!freshUiGuard
          && (hoveredEntity_.kind != HoveredEntity::Kind::None || hoveredTile_.hit)
          && network_.status() == net::Connection::Connected
          && !ui::ctxMenu().open) {
        bool dispatched = false;

        // 1. Entity AABB hit
        if (hoveredEntity_.kind == HoveredEntity::Kind::Npc) {
          for (const auto& n : npcs_) {
            if (n.id != hoveredEntity_.id || n.dying) continue;
            if (n.kind == "chicken") network_.sendAttackNpc(n.id);
            else                     network_.sendTalkTo(n.id);
            oneShotClip_.clear();
            dispatched = true; clickFeedbackColor_ = 1;
            break;
          }
        } else if (hoveredEntity_.kind == HoveredEntity::Kind::DroppedItem) {
          for (const auto& it : droppedItems_) {
            if (it.id != hoveredEntity_.id) continue;
            network_.sendTakeItem(it.id);
            oneShotClip_.clear();
            dispatched = true; clickFeedbackColor_ = 1;
            break;
          }
        } else if (hoveredEntity_.kind == HoveredEntity::Kind::Obstacle) {
          const int tx = hoveredEntity_.tileX, ty = hoveredEntity_.tileY;
          if (ty >= 0 && ty < static_cast<int>(map_.tiles.size()) &&
              tx >= 0 && tx < static_cast<int>(map_.tiles[ty].size())) {
            const auto obs = map_.tiles[ty][tx].obstacle;
            if (obs == shared::ObstacleType::tree) {
              network_.sendChopTree(tx, ty);
              oneShotClip_.clear();
              dispatched = true; clickFeedbackColor_ = 1;
            } else if (obs == shared::ObstacleType::rock) {
              network_.sendMineRock(tx, ty);
              oneShotClip_.clear();
              dispatched = true; clickFeedbackColor_ = 1;
            } else if (obs == shared::ObstacleType::chest) {
              network_.sendOpenBank();
              bankOpen_ = true;
              dispatched = true; clickFeedbackColor_ = 1;
            }
          }
        }
        // RemotePlayer — falls through to walk

        // 2. Fallback: walk to terrain tile
        if (!dispatched && hoveredTile_.hit) {
          const int tx = hoveredTile_.tileX, ty = hoveredTile_.tileY;
          if (ty >= 0 && ty < static_cast<int>(map_.tiles.size()) &&
              tx >= 0 && tx < static_cast<int>(map_.tiles[ty].size()) &&
              map_.tiles[ty][tx].walkable) {
            network_.sendMoveTo(tx, ty);
            oneShotClip_.clear();
            clickFeedbackColor_ = 0;
          } else {
            clickFeedbackColor_ = 1;
          }
        }

        // Click feedback
        ui::clickFeedbackSpawn(pendingWorldClickX_, pendingWorldClickY_, clickFeedbackColor_);
        clickFeedbackActive_ = true;
        clickFeedbackTime_   = std::chrono::steady_clock::now();
        clickFeedbackX_      = pendingWorldClickX_;
        clickFeedbackY_      = pendingWorldClickY_;
      }
    }

    // Dispatch Clay context menu click
    ui::CtxMenuState& cm = ui::ctxMenu();
    if (cm.clickedIndex >= 0 &&
        cm.clickedIndex < static_cast<int>(cm.entries.size())) {
      const auto& e = cm.entries[cm.clickedIndex];

      // ── Inventory slot context menu ────────────────────────────────────────
      if (cm.inventoryCtxSlot >= 0) {
        int slot = cm.inventoryCtxSlot;
        if (e.verb == "Drop") {
          network_.sendDropItem(slot);
        } else if (e.verb == "Examine") {
          if (!cm.contextItemId.empty()) {
            const std::string msg = "It's a " + ui::itemName(cm.contextItemId) + ".";
            chatLog_.appendSystem(msg);
            ui::chatAppendSystem(msg);
          }
        } else {
          // Primary verb: Wield / Wear / Eat → equip
          network_.sendEquipItem(slot);
        }
        cm.inventoryCtxSlot = -1;
        cm.contextItemId.clear();
        cm.clickedIndex = -1;
      }
      // ── Equipment slot context menu ────────────────────────────────────────
      else if (!cm.equipCtxSlot.empty()) {
        if (e.verb == "Remove") {
          network_.sendUnequipItem(cm.equipCtxSlot);
        } else if (e.verb == "Examine") {
          if (!cm.contextItemId.empty()) {
            const std::string msg = "It's a " + ui::itemName(cm.contextItemId) + ".";
            chatLog_.appendSystem(msg);
            ui::chatAppendSystem(msg);
          }
        }
        cm.equipCtxSlot.clear();
        cm.contextItemId.clear();
        cm.clickedIndex = -1;
      }
      // ── Bank grid slot context menu ────────────────────────────────────────
      else if (cm.bankGridCtxSlot >= 0) {
        int slot = cm.bankGridCtxSlot;
        if (e.verb == "Withdraw 1") {
          network_.sendWithdrawItem(slot, 1);
        } else if (e.verb == "Withdraw All") {
          if (currLocalPlayer_ && slot < static_cast<int>(currLocalPlayer_->bank.size())) {
            const auto& opt = currLocalPlayer_->bank[slot];
            if (opt.has_value()) network_.sendWithdrawItem(slot, opt->quantity);
          }
        } else if (e.verb == "Examine") {
          if (!cm.contextItemId.empty()) {
            const std::string msg = "It's a " + ui::itemName(cm.contextItemId) + ".";
            chatLog_.appendSystem(msg);
            ui::chatAppendSystem(msg);
          }
        }
        cm.bankGridCtxSlot = -1;
        cm.contextItemId.clear();
        cm.clickedIndex = -1;
      }
      // ── Bank inventory slot context menu ───────────────────────────────────
      else if (cm.bankInvCtxSlot >= 0) {
        int slot = cm.bankInvCtxSlot;
        if (e.verb == "Deposit 1") {
          network_.sendDepositItem(slot, 1);
        } else if (e.verb == "Deposit All") {
          if (currLocalPlayer_ && slot < static_cast<int>(currLocalPlayer_->inventory.size())) {
            const auto& opt = currLocalPlayer_->inventory[slot];
            if (opt.has_value()) network_.sendDepositItem(slot, opt->quantity);
          }
        }
        cm.bankInvCtxSlot = -1;
        cm.contextItemId.clear();
        cm.clickedIndex = -1;
      }
      // ── World context menu ─────────────────────────────────────────────────
      else if (e.verb == "Chop down") {
        network_.sendChopTree(ctxMenuTileX_, ctxMenuTileY_); oneShotClip_.clear();
      } else if (e.verb == "Mine") {
        network_.sendMineRock(ctxMenuTileX_, ctxMenuTileY_); oneShotClip_.clear();
      } else if (e.verb == "Bank") {
        network_.sendOpenBank(); bankOpen_ = true;
      } else if (e.verb == "Attack") {
        for (const auto& n : npcs_)
          if (n.tileX == ctxMenuTileX_ && n.tileY == ctxMenuTileY_ && !n.dying)
            { network_.sendAttackNpc(n.id); oneShotClip_.clear(); break; }
      } else if (e.verb == "Talk-to") {
        for (const auto& n : npcs_)
          if (n.tileX == ctxMenuTileX_ && n.tileY == ctxMenuTileY_ && !n.dying)
            { network_.sendTalkTo(n.id); oneShotClip_.clear(); break; }
      } else if (e.verb == "Examine" && !ctxMenuPlayerId_.empty()) {
        // Player examine — must be checked before the generic NPC examine below.
        auto rpIt = currRemotePlayers_.find(ctxMenuPlayerId_);
        if (rpIt != currRemotePlayers_.end()) {
          std::string msg = "It's " + rpIt->second.playerName + "!";
          chatLog_.appendSystem(msg.c_str());
          ui::chatAppendSystem(msg.c_str());
        }
        ctxMenuPlayerId_.clear();
      } else if (e.verb == "Trade with" || e.verb == "Follow") {
        // Not yet implemented server-side — no-op.
      } else if (e.verb == "Examine") {
        for (const auto& n : npcs_)
          if (n.tileX == ctxMenuTileX_ && n.tileY == ctxMenuTileY_ && !n.dying) {
            const char* txt = (n.kind=="chicken")    ? "It's a chicken."
                            : (n.kind=="shopkeeper") ? "This is a friendly shopkeeper."
                            : "An NPC.";
            chatLog_.appendSystem(txt);
            ui::chatAppendSystem(txt);
            break;
          }
        // Also handle obstacle examine
        if (ctxMenuTileY_ >= 0 && ctxMenuTileY_ < static_cast<int>(map_.tiles.size()) &&
            ctxMenuTileX_ >= 0 && ctxMenuTileX_ < static_cast<int>(map_.tiles[ctxMenuTileY_].size())) {
          const auto obs = map_.tiles[ctxMenuTileY_][ctxMenuTileX_].obstacle;
          if (obs == shared::ObstacleType::tree) {
            chatLog_.appendSystem("A sturdy tree.");
            ui::chatAppendSystem("A sturdy tree.");
          } else if (obs == shared::ObstacleType::rock) {
            chatLog_.appendSystem("A rocky outcrop.");
            ui::chatAppendSystem("A rocky outcrop.");
          } else if (obs == shared::ObstacleType::chest) {
            chatLog_.appendSystem("A secure bank chest.");
            ui::chatAppendSystem("A secure bank chest.");
          }
        }
      } else if (e.verb == "Take") {
        // payload holds the specific item id (set at menu-build time).
        // Fall back to first-on-tile if somehow missing (shouldn't happen).
        const std::string& takeId = e.payload;
        if (!takeId.empty()) {
          network_.sendTakeItem(takeId); oneShotClip_.clear();
        } else {
          for (const auto& it : droppedItems_)
            if (it.tileX == ctxMenuTileX_ && it.tileY == ctxMenuTileY_)
              { network_.sendTakeItem(it.id); oneShotClip_.clear(); break; }
        }
      } else if (e.verb == "Walk here") {
        network_.sendMoveTo(ctxMenuTileX_, ctxMenuTileY_); oneShotClip_.clear();
      }
      cm.clickedIndex = -1;
    }
  }

  const bool connected = (network_.status() == net::Connection::Connected);

  // ── Dispatch Clay modal results ─────────────────────────────────────────────
  if (showClayUi_) {
    // Login modal: submitted → call network
    const auto& lf = ui::loginFormState();
    if (lf.submitted) {
      if (lf.registerMode)
        network_.registerAndConnect(lf.host, lf.port, lf.username, lf.password);
      else
        network_.loginAndConnect(lf.host, lf.port, lf.username, lf.password);
    }
    // Join modal: submitted → send SET_NAME
    const auto& jf = ui::joinFormState();
    if (jf.submitted && jf.name[0] != '\0') {
      char buf[80];
      std::snprintf(buf, sizeof(buf),
                    "{\"type\":\"SET_NAME\",\"playerName\":\"%s\"}", jf.name);
      network_.sendActionRaw(buf);
      isNewPlayer_ = false;
    }
  }

  // ImGui modals: only shown when Clay UI is off (debug fallback)
  if (!showClayUi_) {
    drawLoginUi();
    if (connected && isNewPlayer_) drawJoinModal();
  }

  // ── Clay bank close ──────────────────────────────────────────────────────────
  if (showClayUi_ && ui::bankWantsClose()) {
    bankOpen_ = false;
    network_.sendCloseBank();
  }

  if (connected && currLocalPlayer_) {
    // Bank panel is now rendered by Clay when Clay UI is on.
    // Fall back to ImGui bank only when Clay UI is disabled.
    if (showImguiUi_ && !showClayUi_) {
      ui::drawBankPanel (*currLocalPlayer_, &network_, &bankOpen_);
    }
    // ImGui chat: fallback when Clay UI is disabled.
    if (showImguiUi_ && !showClayUi_) {
      chatLog_.draw(&network_);
    }

    // ---- Build overlay entries from per-frame interpolated positions -------
    // All positions are lerped with the same tick alpha used for entity render,
    // so health bars and chat bubbles track the animated models exactly.
    {
      const auto  nowOv  = std::chrono::steady_clock::now();
      const auto  dtMsOv = std::chrono::duration_cast<std::chrono::milliseconds>(
                               nowOv - lastTickTime_).count();
      const float tickAlpha = std::clamp(
          static_cast<float>(dtMsOv) / static_cast<float>(shared::kTickDurationMs),
          0.0f, 1.0f);

      // Chat-bubble alpha: visible for 50 ticks, fades over last 10
      auto chatAlphaFor = [&](const std::string& msg, int msgTick) -> float {
        if (msg.empty() || msgTick <= 0) return 0.0f;
        const int age = currentTick_ - msgTick;
        if (age < 0 || age > 50) return 0.0f;
        return (age > 40) ? 1.0f - static_cast<float>(age - 40) / 10.0f : 1.0f;
      };

      // --- Local player entry ---
      ui::WorldOverlays::OverlayEntry localEntry;
      {
        float lx = static_cast<float>(currLocalPlayer_->tileX);
        float lz = static_cast<float>(currLocalPlayer_->tileY);
        if (prevLocalPlayer_) {
          lx = std::lerp(static_cast<float>(prevLocalPlayer_->tileX), lx, tickAlpha);
          lz = std::lerp(static_cast<float>(prevLocalPlayer_->tileY), lz, tickAlpha);
        }
        const float ly = tileWorldY(map_,
                                    static_cast<int>(std::round(lx)),
                                    static_cast<int>(std::round(lz)));
        localEntry.wx      = lx;
        localEntry.wy      = ly;
        localEntry.wz      = lz;
        localEntry.hp      = currLocalPlayer_->hp;
        localEntry.maxHp   = currLocalPlayer_->maxHp;
        localEntry.showHpBar   = (currLocalPlayer_->maxHp > 0);
        localEntry.chatMessage = currLocalPlayer_->chatMessage;
        localEntry.chatAlpha   = chatAlphaFor(currLocalPlayer_->chatMessage,
                                              currLocalPlayer_->chatMessageTick);
      }

      // --- NPC + remote player entries ---
      std::vector<ui::WorldOverlays::OverlayEntry> entityEntries;
      entityEntries.reserve(currNpcs_.size() + currRemotePlayers_.size());

      for (const auto& [id, curr] : currNpcs_) {
        if (curr.dying) continue;
        float fx = static_cast<float>(curr.tileX);
        float fz = static_cast<float>(curr.tileY);
        auto pit = prevNpcs_.find(id);
        if (pit != prevNpcs_.end()) {
          fx = std::lerp(static_cast<float>(pit->second.tileX), fx, tickAlpha);
          fz = std::lerp(static_cast<float>(pit->second.tileY), fz, tickAlpha);
        }
        const float fy = tileWorldY(map_,
                                    static_cast<int>(std::round(fx)),
                                    static_cast<int>(std::round(fz)));
        ui::WorldOverlays::OverlayEntry e;
        e.id         = id;
        e.wx         = fx;
        e.wy         = fy;
        e.wz         = fz;
        e.hp         = curr.hp;
        e.maxHp      = curr.maxHp;
        e.showHpBar  = (curr.maxHp > 0 && curr.hp < curr.maxHp);
        // NPCState has no chatMessage field — NPCs don't chat.
        entityEntries.push_back(std::move(e));
      }

      // Remote players: chat bubbles only (no HP bar shown for other players)
      for (const auto& [id, rp] : currRemotePlayers_) {
        if (rp.dying) continue;
        const float ca = chatAlphaFor(rp.chatMessage, rp.chatMessageTick);
        if (ca <= 0.0f) continue;
        float fx = static_cast<float>(rp.tileX);
        float fz = static_cast<float>(rp.tileY);
        auto pit = prevRemotePlayers_.find(id);
        if (pit != prevRemotePlayers_.end()) {
          fx = std::lerp(static_cast<float>(pit->second.tileX), fx, tickAlpha);
          fz = std::lerp(static_cast<float>(pit->second.tileY), fz, tickAlpha);
        }
        const float fy = tileWorldY(map_,
                                    static_cast<int>(std::round(fx)),
                                    static_cast<int>(std::round(fz)));
        ui::WorldOverlays::OverlayEntry e;
        e.wx          = fx;
        e.wy          = fy;
        e.wz          = fz;
        e.chatMessage = rp.chatMessage;
        e.chatAlpha   = ca;
        entityEntries.push_back(std::move(e));
      }

      overlays_.draw(viewProj, fbW, fbH, &localEntry, entityEntries);
    }

    // Context info + tooltip are rendered by Clay when Clay UI is on.
    // Fallback ImGui tooltip when Clay UI is disabled.
    if (!showClayUi_ &&
        hoveredTile_.hit &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
      const int tx = hoveredTile_.tileX;
      const int ty = hoveredTile_.tileY;
      static std::string tooltipNameStr;
      const char* tooltipName = nullptr;
      for (const auto& n : npcs_) {
        if (n.tileX == tx && n.tileY == ty && !n.dying) {
          tooltipNameStr = ui::npcName(n.kind);
          tooltipName = tooltipNameStr.c_str();
          break;
        }
      }
      if (!tooltipName &&
          ty >= 0 && ty < static_cast<int>(map_.tiles.size()) &&
          tx >= 0 && tx < static_cast<int>(map_.tiles[ty].size())) {
        const auto obs = map_.tiles[ty][tx].obstacle;
        if      (obs == shared::ObstacleType::tree)  tooltipName = "Tree";
        else if (obs == shared::ObstacleType::rock)  tooltipName = "Rock";
        else if (obs == shared::ObstacleType::chest) tooltipName = "Chest";
      }
      if (!tooltipName) {
        for (const auto& di : droppedItems_) {
          if (di.tileX == tx && di.tileY == ty) {
            tooltipNameStr = ui::itemName(di.itemId);
            tooltipName = tooltipNameStr.c_str();
            break;
          }
        }
      }
      if (tooltipName) {
        const ImGuiIO& io2 = ImGui::GetIO();
        ImVec2 ttPos { io2.MousePos.x + 16.0f, io2.MousePos.y + 16.0f };
        const ImVec2 textSz = ImGui::CalcTextSize(tooltipName);
        if (ttPos.x + textSz.x + 8.0f > io2.DisplaySize.x)
          ttPos.x = io2.MousePos.x - textSz.x - 8.0f;
        ImDrawList* dl2 = ImGui::GetForegroundDrawList();
        dl2->AddRectFilled(ImVec2(ttPos.x - 4, ttPos.y - 2),
                           ImVec2(ttPos.x + textSz.x + 4, ttPos.y + textSz.y + 2),
                           IM_COL32(15, 8, 2, 220));
        dl2->AddRect(ImVec2(ttPos.x - 4, ttPos.y - 2),
                     ImVec2(ttPos.x + textSz.x + 4, ttPos.y + textSz.y + 2),
                     IM_COL32(107, 79, 41, 200));
        dl2->AddText(ttPos, IM_COL32(240, 206, 96, 255), tooltipName);
      }
    }

  }

  // ---- Click feedback marker -----------------------------------------------
  // When Clay UI is on: rendered by buildClickFeedback() inside clayFrame.
  // When Clay UI is off: fallback ImGui DrawList circle.
  if (!showClayUi_ && clickFeedbackActive_) {
    const float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - clickFeedbackTime_).count();
    constexpr float kDuration = 0.45f;
    if (elapsed > kDuration) {
      clickFeedbackActive_ = false;
    } else {
      const float t = elapsed / kDuration;
      const float radius = 9.0f * (1.0f + 0.6f * t);
      const float alpha = 1.0f - t;
      ImU32 color = (clickFeedbackColor_ == 0)
          ? IM_COL32(255, 220, 50, static_cast<int>(alpha * 200))
          : IM_COL32(200, 50, 50, static_cast<int>(alpha * 200));
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      dl->AddCircle(ImVec2(clickFeedbackX_, clickFeedbackY_),
                    radius, color, 24, 2.0f);
    }
  }

  if (ImGui::Begin("Debug")) {
    ImGui::Text("GL %s", glGetString(GL_VERSION));
    ImGui::Text("Framebuffer: %d x %d", fbW, fbH);
    ImGui::Text("MSAA: %dx", msaa_->samples());
    ImGui::Separator();
    ImGui::Text("Map: %d x %d tiles  (seed %u)", terrainTileW_, terrainTileH_, mapSeed_);
    ImGui::Text("Tris/tile: 2   Indices: %d", terrainIndexCt_);
    ImGui::Text("Obstacles: %zu trees, %zu rocks  (instanced)",
                obstacles_.treeCount(), obstacles_.rockCount());
    ImGui::Text("Entities: %zu NPCs, %zu dropped items",
                entities_.npcCount(), entities_.itemCount());
    if (ImGui::Button("Regenerate (next seed)")) {
      ++mapSeed_;
      generateAndBuildTerrain();
    }
    if (ImGui::SliderFloat("Noise frequency", &noiseFreq_,
                           0.005f, 0.30f, "%.3f",
                           ImGuiSliderFlags_Logarithmic)) {
      generateAndBuildTerrain();
    }
    if (ImGui::SliderFloat("Noise amplitude", &noiseAmp_,
                           0.0f, 4.0f, "%.2f")) {
      generateAndBuildTerrain();
    }
    ImGui::Checkbox("Wireframe overlay", &wireframe_);
    ImGui::Separator();
    ImGui::TextUnformatted("Camera");
    if (hoveredTile_.hit) {
      ImGui::Text("Hover: tile (%d, %d)  world (%.2f, %.2f, %.2f)",
                  hoveredTile_.tileX, hoveredTile_.tileY,
                  hoveredTile_.worldPos.x, hoveredTile_.worldPos.y, hoveredTile_.worldPos.z);
    } else {
      ImGui::TextUnformatted("Hover: (cursor off terrain)");
    }
    const glm::vec3 eye = camera_.cameraPosition();
    ImGui::Text("Eye:  %.1f %.1f %.1f  %s", eye.x, eye.y, eye.z,
                camera_.isDragging() ? "(rotating)" : "");
    ImGui::TextUnformatted("Middle-drag: rotate, wheel: zoom, arrows: rotate");

    ImGui::Separator();
    ImGui::TextUnformatted("Audio (Phase 9)");
    {
      float vol = audio_.masterVolume();
      if (ImGui::SliderFloat("Master volume", &vol, 0.0f, 1.0f, "%.2f")) {
        audio_.setMasterVolume(vol);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Test")) audio_.playHit();
      ImGui::Text("Status: %s", audio_.isReady() ? "ready" : "unavailable");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Lighting (Phase 6)");
    ImGui::Checkbox("Directional lighting", &lightingEnabled_);
    ImGui::BeginDisabled(!lightingEnabled_);
    ImGui::SliderFloat("Sun yaw (deg)",   &sunYawDeg_,   0.0f, 360.0f, "%.0f");
    ImGui::SliderFloat("Sun pitch (deg)", &sunPitchDeg_, 0.0f,  90.0f, "%.0f");
    ImGui::SliderFloat("Ambient",         &ambient_,     0.0f,   1.0f, "%.2f");
    ImGui::SliderFloat("Diffuse",         &diffuse_,     0.0f,   1.5f, "%.2f");
    if (ImGui::SmallButton("Defaults")) {
      sunYawDeg_   = 200.0f;
      sunPitchDeg_ = 58.0f;
      ambient_     = 0.45f;
      diffuse_     = 0.55f;
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Shadows (Phase 6b)");
    ImGui::Checkbox("Directional shadow map", &shadowsEnabled_);
    ImGui::BeginDisabled(!shadowsEnabled_);
    ImGui::SliderFloat("Darkness",     &shadowDarkness_,   0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Bias",         &shadowBias_,    0.0001f, 0.02f, "%.4f");
    ImGui::SliderFloat("Half-extent",  &shadowHalfExtent_, 10.0f, 80.0f, "%.0f");
    ImGui::Text("Resolution: %d x %d", shadowMap_.size(), shadowMap_.size());
    if (ImGui::SmallButton("Shadow defaults")) {
      shadowDarkness_   = 0.55f;
      shadowBias_       = 0.0025f;
      shadowHalfExtent_ = 40.0f;
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("HSL palette (Phase 7)");
    ImGui::Checkbox("Quantize", &palette_);
    ImGui::BeginDisabled(!palette_);
    ImGui::SliderInt("Hue levels",   &paletteHues_, 1, 64);
    ImGui::SliderInt("Sat levels",   &paletteSats_, 1, 32);
    ImGui::SliderInt("Lum levels",   &paletteLums_, 1, 64);
    if (ImGui::SmallButton("Default (64/16/48)")) {
      paletteHues_ = 64; paletteSats_ = 16; paletteLums_ = 48;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Crunchy (8/4/6)")) {
      paletteHues_ = 8;  paletteSats_ = 4;  paletteLums_ = 6;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Smooth (64/16/64)")) {
      paletteHues_ = 64; paletteSats_ = 16; paletteLums_ = 64;
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Hover Outline");
    ImGui::SliderFloat("Outline radius (px)", &outlineRadius_,    1.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("Depth bias",          &outlineDepthBias_, 0.0f,  0.01f, "%.4f");
    ImGui::ColorEdit4("Outline color",  reinterpret_cast<float*>(&outlineColor_));
    ImGui::ColorEdit4("Tile hover color", reinterpret_cast<float*>(&hoverTileColor_));
    if (ImGui::SmallButton("Reset outline defaults")) {
      outlineRadius_    = 3.0f;
      outlineDepthBias_ = 0.002f;
      outlineColor_     = {0.0f, 0.9f, 0.9f, 0.95f};
      hoverTileColor_   = {1.0f, 0.85f, 0.10f, 1.0f};
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Fog");
    ImGui::Checkbox("Enable fog",      &fogEnabled_);
    ImGui::BeginDisabled(!fogEnabled_);
    ImGui::SliderFloat("Density",      &fogDensity_, 0.0f,  0.1f,  "%.4f");
    ImGui::SliderFloat("Start dist",   &fogStart_,   0.0f,  120.0f, "%.1f");
    ImGui::ColorEdit3("Fog color",     reinterpret_cast<float*>(&fogColor_));
    if (ImGui::SmallButton("Fog defaults")) {
      fogDensity_ = 0.015f; fogStart_ = 5.0f;
      fogColor_ = {0.58f, 0.67f, 0.78f};
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Ambient Occlusion");
    ImGui::Checkbox("Enable AO",       &aoEnabled_);
    ImGui::BeginDisabled(!aoEnabled_);
    ImGui::SliderFloat("AO strength",  &aoStrength_, 0.0f,  1.0f,  "%.2f");
    if (ImGui::SmallButton("AO defaults")) { aoStrength_ = 0.50f; }
    ImGui::Separator();
    ImGui::EndDisabled();
    if (aoEnabled_) ImGui::TextDisabled("AO baked into terrain mesh — regen to update");

    ImGui::Separator();
    if (ImGui::Button("Save as default")) saveSettings();
    ImGui::SameLine();
    ImGui::TextDisabled("Writes settings.cfg next to exe");

    ImGui::Separator();
    ImGui::TextUnformatted("UI Layer");
    ImGui::Checkbox("ImGui UI", &showImguiUi_);
    ImGui::SameLine();
    ImGui::Checkbox("Clay UI", &showClayUi_);

    ImGui::Separator();
    ImGui::TextUnformatted("Network (Phase 4)");
    const auto status = network_.status();
    const char* statusText = "Disconnected";
    switch (status) {
      case net::Connection::LoggingIn:    statusText = "Logging in...";  break;
      case net::Connection::Connecting:   statusText = "Connecting...";  break;
      case net::Connection::Connected:    statusText = "Connected";      break;
      case net::Connection::Failed:       statusText = "Failed";         break;
      case net::Connection::Disconnected: statusText = "Disconnected";   break;
    }
    ImGui::Text("Status: %s", statusText);
    if (!network_.lastError().empty() && status == net::Connection::Failed) {
      ImGui::TextWrapped("Error: %s", network_.lastError().c_str());
    }
    if (status == net::Connection::Connected) {
      if (ImGui::Button(bankOpen_ ? "Close bank" : "Open bank")) {
        bankOpen_ = !bankOpen_;
        if (bankOpen_) network_.sendOpenBank();
      }
      ImGui::Text("Player: %s  (tick %d)", network_.playerName().c_str(), currentTick_);
      if (currLocalPlayer_) {
        ImGui::Text("Tile: (%d, %d)  hp %d/%d",
                    currLocalPlayer_->tileX, currLocalPlayer_->tileY,
                    currLocalPlayer_->hp, currLocalPlayer_->maxHp);
        ImGui::TextUnformatted("Left-click a tile to walk there.");
      } else {
        ImGui::TextUnformatted("Waiting for first state tick...");
      }
    }
  }
  ImGui::End();

  // Context menu: Clay path is handled inside clayFrame; ImGui fallback when Clay off.
  if (!showClayUi_) drawWorldContextMenu();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// =====================================================================
// Right-click context menu — Phase 8b-ii.
// =====================================================================
//
// The menu opens at the cursor when the user right-clicks an in-world
// location. Its contents are computed from the latched tile + the entities
// (NPCs, dropped items, obstacles) that occupy it at that moment. The
// server validates the action; the client never gates "is this valid right
// now" — clicking Chop on an already-depleted tree is a no-op server-side.
void App::drawWorldContextMenu() {
  // Promote a click into an open popup. We delay this to the UI pass so
  // ImGui's per-frame popup state is in the right place.
  if (ctxMenuRequest_) {
    ctxMenuRequest_ = false;
    if (ctxMenuTileHit_) ImGui::OpenPopup("world_ctx");
  }
  if (!ImGui::BeginPopup("world_ctx")) return;

  // OSRS-style header
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::TextUnformatted("Choose Option");
  ImGui::PopStyleColor();
  ImGui::Separator();

  // ---- Tile obstacle ------------------------------------------------------
  shared::ObstacleType obstacle = shared::ObstacleType::none;
  if (ctxMenuTileY_ >= 0 && ctxMenuTileY_ < static_cast<int>(map_.tiles.size()) &&
      ctxMenuTileX_ >= 0 && ctxMenuTileX_ < static_cast<int>(map_.tiles[ctxMenuTileY_].size())) {
    obstacle = map_.tiles[ctxMenuTileY_][ctxMenuTileX_].obstacle;
  }
  switch (obstacle) {
    case shared::ObstacleType::tree:
      if (ImGui::Selectable("Chop down  Tree")) {
        network_.sendChopTree(ctxMenuTileX_, ctxMenuTileY_);
        oneShotClip_.clear();
      }
      if (ImGui::Selectable("Examine  Tree"))
        chatLog_.appendSystem("A sturdy tree.");
      break;
    case shared::ObstacleType::rock:
      if (ImGui::Selectable("Mine  Rock")) {
        network_.sendMineRock(ctxMenuTileX_, ctxMenuTileY_);
        oneShotClip_.clear();
      }
      if (ImGui::Selectable("Examine  Rock"))
        chatLog_.appendSystem("A rocky outcrop.");
      break;
    case shared::ObstacleType::chest:
      if (ImGui::Selectable("Bank  Chest")) {
        network_.sendOpenBank();
        bankOpen_ = true;
      }
      if (ImGui::Selectable("Examine  Chest"))
        chatLog_.appendSystem("A secure bank chest.");
      break;
    default: break;
  }

  // ---- NPCs at this tile --------------------------------------------------
  for (const auto& n : npcs_) {
    if (n.tileX != ctxMenuTileX_ || n.tileY != ctxMenuTileY_) continue;
    if (n.dying) continue;
    const std::string displayName = ui::npcName(n.kind);
    char buf[128];
    if (ui::npcIsAttackable(n.kind)) {
      std::snprintf(buf, sizeof(buf), "Attack  %s", displayName.c_str());
      if (ImGui::Selectable(buf)) { network_.sendAttackNpc(n.id); oneShotClip_.clear(); }
    } else {
      std::snprintf(buf, sizeof(buf), "Talk-to  %s", displayName.c_str());
      if (ImGui::Selectable(buf)) { network_.sendTalkTo(n.id); oneShotClip_.clear(); }
    }
    const char* examineText = (n.kind == "chicken")    ? "It's a chicken."
                            : (n.kind == "shopkeeper") ? "This is a friendly shopkeeper."
                            : "An NPC.";
    std::snprintf(buf, sizeof(buf), "Examine  %s", displayName.c_str());
    if (ImGui::Selectable(buf)) chatLog_.appendSystem(examineText);
  }

  // ---- Dropped items at this tile ----------------------------------------
  for (const auto& it : droppedItems_) {
    if (it.tileX != ctxMenuTileX_ || it.tileY != ctxMenuTileY_) continue;
    ImGui::PushID(it.id.c_str());
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Take  %s", ui::itemName(it.itemId).c_str());
    if (ImGui::Selectable(buf)) { network_.sendTakeItem(it.id); oneShotClip_.clear(); }
    ImGui::PopID();
  }

  // ---- Always available --------------------------------------------------
  if (ImGui::Selectable("Walk here")) {
    network_.sendMoveTo(ctxMenuTileX_, ctxMenuTileY_);
    oneShotClip_.clear();
  }

  ImGui::EndPopup();
}

// =====================================================================
// Level editor — export current procedural map as worldMap.json
// =====================================================================
void App::exportWorldMap() {
  const auto outPath = resolveFromExe("worldMap.json");
  FILE* f = nullptr;
#ifdef _WIN32
  _wfopen_s(&f, outPath.wstring().c_str(), L"wb");
#else
  f = std::fopen(outPath.string().c_str(), "wb");
#endif
  if (!f) {
    chatLog_.appendSystem("Failed to open worldMap.json for writing.");
    return;
  }

  const int W = map_.width;
  const int H = map_.height;

  std::fprintf(f, "{\"version\":2,\"width\":%d,\"height\":%d,\n", W, H);

  // tiles[y][x]
  std::fprintf(f, "\"tiles\":[\n");
  for (int y = 0; y < H; ++y) {
    std::fprintf(f, "  [");
    for (int x = 0; x < W; ++x) {
      const auto& t = map_.tiles[y][x];
      const char* typeStr = "grass";
      switch (t.type) {
        case shared::TileType::dirt:  typeStr = "dirt";  break;
        case shared::TileType::stone: typeStr = "stone"; break;
        case shared::TileType::water: typeStr = "water"; break;
        case shared::TileType::cliff: typeStr = "cliff"; break;
        case shared::TileType::wall:  typeStr = "wall";  break;
        case shared::TileType::door:  typeStr = "door";  break;
        default: break;
      }
      const char* obsStr = "none";
      switch (t.obstacle) {
        case shared::ObstacleType::tree:         obsStr = "tree";         break;
        case shared::ObstacleType::rock:         obsStr = "rock";         break;
        case shared::ObstacleType::chest:        obsStr = "chest";        break;
        case shared::ObstacleType::fishing_spot: obsStr = "fishing_spot"; break;
        default: break;
      }
      std::fprintf(f,
          "{\"x\":%d,\"y\":%d,\"walkable\":%s,\"type\":\"%s\","
          "\"obstacle\":\"%s\",\"blocksRanged\":%s,"
          "\"groundColor\":\"%s\",\"height\":%.3f}",
          t.x, t.y,
          t.walkable ? "true" : "false",
          typeStr, obsStr,
          t.blocksRanged ? "true" : "false",
          t.groundColor.c_str(),
          t.height);
      if (x < W - 1) std::fprintf(f, ",");
    }
    std::fprintf(f, "]%s\n", (y < H - 1) ? "," : "");
  }
  std::fprintf(f, "],\n");

  // npcSpawns — empty for now (server has defaults)
  std::fprintf(f, "\"npcSpawns\":[],\n");
  std::fprintf(f, "\"permanentItems\":[],\n");

  // vertexHeights
  std::fprintf(f, "\"vertexHeights\":[");
  for (std::size_t i = 0; i < map_.vertexHeights.size(); ++i) {
    std::fprintf(f, "%.6f", static_cast<double>(map_.vertexHeights[i]));
    if (i < map_.vertexHeights.size() - 1) std::fprintf(f, ",");
  }
  std::fprintf(f, "]\n}\n");
  std::fclose(f);

  chatLog_.appendSystem("Exported worldMap.json (" + std::to_string(W) + "x" +
                        std::to_string(H) + ") next to exe.");
  std::fprintf(stdout, "[App] Exported worldMap.json to %s\n", outPath.string().c_str());
}

void App::onResize(int width, int height) {
  if (msaa_) msaa_->resize(width, height);
  if (width > 0 && height > 0) {
    initOutlineMaskFbo(width, height);
    ui::clayResize(width, height);
  }
}

void App::initOutlineMaskFbo(int w, int h) {
  if (outlineMaskFbo_) { glDeleteFramebuffers(1, &outlineMaskFbo_); outlineMaskFbo_ = 0; }
  if (outlineMaskTex_) { glDeleteTextures(1, &outlineMaskTex_);     outlineMaskTex_ = 0; }

  glCreateTextures(GL_TEXTURE_2D, 1, &outlineMaskTex_);
  glTextureStorage2D(outlineMaskTex_, 1, GL_R8, w, h);
  glTextureParameteri(outlineMaskTex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTextureParameteri(outlineMaskTex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTextureParameteri(outlineMaskTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(outlineMaskTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glCreateFramebuffers(1, &outlineMaskFbo_);
  glNamedFramebufferTexture(outlineMaskFbo_, GL_COLOR_ATTACHMENT0, outlineMaskTex_, 0);
}

void App::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigDebugHighlightIdConflicts = false;

  // OSRS pixel font — ProggyClean at 13px gives a retro game feel.
  // Falls back gracefully to ImGui's built-in bitmap font if the file isn't found.
  const auto fontPath = resolveFromExe("assets/ProggyClean.ttf");
  if (std::filesystem::exists(fontPath)) {
    io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 13.0f);
  } else {
    io.Fonts->AddFontDefault();
  }

  // OSRS-inspired dark brown / gold theme. OSRS has zero rounding — square
  // corners everywhere — which is key to avoiding the "debug tool" look.
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding    = 0.0f;
  s.FrameRounding     = 0.0f;
  s.GrabRounding      = 0.0f;
  s.ScrollbarRounding = 0.0f;
  s.TabRounding       = 0.0f;
  s.PopupRounding     = 0.0f;
  s.ChildRounding     = 0.0f;
  s.WindowBorderSize  = 1.0f;
  s.FrameBorderSize   = 1.0f;
  s.ItemSpacing       = ImVec2(4.0f, 4.0f);
  s.FramePadding      = ImVec2(5.0f, 3.0f);
  s.WindowPadding     = ImVec2(6.0f, 6.0f);
  s.ScrollbarSize     = 8.0f;
  s.GrabMinSize       = 6.0f;

  ImVec4* c = s.Colors;
  // OSRS gold text — the single biggest visual differentiator from grey debug UIs
  c[ImGuiCol_Text]                  = ImVec4(0.94f, 0.82f, 0.50f, 1.00f);
  c[ImGuiCol_TextDisabled]          = ImVec4(0.54f, 0.44f, 0.25f, 1.00f);
  // Deep brownstone panel — much darker than default grey, feels like stone
  c[ImGuiCol_WindowBg]              = ImVec4(0.11f, 0.07f, 0.03f, 0.97f);
  c[ImGuiCol_ChildBg]               = ImVec4(0.09f, 0.06f, 0.02f, 0.80f);
  c[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.06f, 0.02f, 0.97f);
  // Visible brownstone border — frames elements without being harsh
  c[ImGuiCol_Border]                = ImVec4(0.42f, 0.31f, 0.16f, 0.90f);
  c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  // Dark slot background — the classic OSRS inventory cell darkness
  c[ImGuiCol_FrameBg]               = ImVec4(0.07f, 0.04f, 0.01f, 0.90f);
  c[ImGuiCol_FrameBgHovered]        = ImVec4(0.15f, 0.09f, 0.03f, 0.90f);
  c[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  // Title bars — slightly lighter brownstone than the window bg
  c[ImGuiCol_TitleBg]               = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_TitleBgActive]         = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.11f, 0.07f, 0.03f, 0.90f);
  c[ImGuiCol_MenuBarBg]             = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.03f, 0.01f, 0.80f);
  c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.42f, 0.31f, 0.16f, 0.90f);
  c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.55f, 0.40f, 0.20f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.70f, 0.52f, 0.25f, 1.00f);
  // OSRS orange-gold accent for interactive elements
  c[ImGuiCol_CheckMark]             = ImVec4(1.00f, 0.65f, 0.15f, 1.00f);
  c[ImGuiCol_SliderGrab]            = ImVec4(1.00f, 0.65f, 0.15f, 0.85f);
  c[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
  c[ImGuiCol_Button]                = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_ButtonHovered]         = ImVec4(0.42f, 0.26f, 0.10f, 1.00f);
  c[ImGuiCol_ButtonActive]          = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_Header]                = ImVec4(0.28f, 0.17f, 0.07f, 0.90f);
  c[ImGuiCol_HeaderHovered]         = ImVec4(0.42f, 0.26f, 0.10f, 0.90f);
  c[ImGuiCol_HeaderActive]          = ImVec4(0.55f, 0.34f, 0.14f, 1.00f);
  c[ImGuiCol_Separator]             = ImVec4(0.42f, 0.31f, 0.16f, 0.60f);
  c[ImGuiCol_SeparatorHovered]      = ImVec4(1.00f, 0.65f, 0.15f, 0.78f);
  c[ImGuiCol_SeparatorActive]       = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
  c[ImGuiCol_ResizeGrip]            = ImVec4(0.28f, 0.17f, 0.07f, 0.50f);
  c[ImGuiCol_ResizeGripHovered]     = ImVec4(1.00f, 0.65f, 0.15f, 0.78f);
  c[ImGuiCol_ResizeGripActive]      = ImVec4(1.00f, 0.75f, 0.25f, 1.00f);
  c[ImGuiCol_Tab]                   = ImVec4(0.15f, 0.09f, 0.03f, 0.95f);
  c[ImGuiCol_TabHovered]            = ImVec4(0.42f, 0.26f, 0.10f, 1.00f);
  c[ImGuiCol_TabActive]             = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TabUnfocused]          = ImVec4(0.10f, 0.06f, 0.02f, 0.95f);
  c[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  c[ImGuiCol_DockingPreview]        = ImVec4(1.00f, 0.65f, 0.15f, 0.70f);
  c[ImGuiCol_PlotLines]             = ImVec4(0.94f, 0.82f, 0.50f, 1.00f);
  c[ImGuiCol_PlotHistogram]         = ImVec4(1.00f, 0.65f, 0.15f, 1.00f);
  c[ImGuiCol_TableHeaderBg]         = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  c[ImGuiCol_TableBorderStrong]     = ImVec4(0.42f, 0.31f, 0.16f, 1.00f);
  c[ImGuiCol_TableBorderLight]      = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
  c[ImGuiCol_TextSelectedBg]        = ImVec4(1.00f, 0.65f, 0.15f, 0.35f);
  c[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 0.65f, 0.15f, 0.90f);
  c[ImGuiCol_NavHighlight]          = ImVec4(1.00f, 0.65f, 0.15f, 1.00f);
  c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.65f);

  // install_callbacks=true chains GLFW callbacks: ImGui's handlers run first,
  // then ours (which were registered in Window::init).
  ImGui_ImplGlfw_InitForOpenGL(window_.handle(), true);
  ImGui_ImplOpenGL3_Init("#version 460 core");
  imguiInited_ = true;
}

void App::shutdownImGui() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  imguiInited_ = false;
}

// =====================================================================
// Player rendering — skinned glTF (Phase 5)
// =====================================================================

void App::renderPlayer(const glm::mat4& viewProj, float dt) {
  if (!currLocalPlayer_) return;
  if (!playerModel_.isLoaded()) return;

  // Smooth-interpolated position from prev/curr server snapshots.
  float fx = static_cast<float>(currLocalPlayer_->tileX);
  float fy = static_cast<float>(currLocalPlayer_->tileY);
  float yWorld = tileWorldY(map_, currLocalPlayer_->tileX, currLocalPlayer_->tileY);
  if (prevLocalPlayer_) {
    const auto now   = std::chrono::steady_clock::now();
    const auto dtMs  = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTickTime_).count();
    const float alpha = std::clamp(static_cast<float>(dtMs) / static_cast<float>(shared::kTickDurationMs),
                                   0.0f, 1.0f);
    fx = std::lerp(static_cast<float>(prevLocalPlayer_->tileX), fx, alpha);
    fy = std::lerp(static_cast<float>(prevLocalPlayer_->tileY), fy, alpha);
    const float prevY = tileWorldY(map_, prevLocalPlayer_->tileX, prevLocalPlayer_->tileY);
    yWorld = std::lerp(prevY, yWorld, alpha);
  }

  // Animation clip & timing. Phase 5e — while a one-shot deadline is in
  // the future, override the movement-driven clip with the attack/chop
  // animation. Falls back through clipForPlayer once the deadline lapses.
  // Pre-compute target yaw here so it's available for turn-suppression below.
  const float targetYaw = facingToYaw(currLocalPlayer_->facing);

  const char* desired = nullptr;
  const auto  now = std::chrono::steady_clock::now();
  if (!oneShotClip_.empty() && now < oneShotEndsAt_) {
    desired = oneShotClip_.c_str();
  } else {
    if (!oneShotClip_.empty()) oneShotClip_.clear();
    const shared::PlayerState* prevPtr =
      prevLocalPlayer_.has_value() ? &*prevLocalPlayer_ : nullptr;
    desired = clipForPlayer(&*currLocalPlayer_, prevPtr);
    // Suppress movement clips until the model has nearly finished turning.
    // Threshold of ~20° means the walk/run animation only starts once the
    // character is visually pointing at the destination, giving a clear
    // "turn first, then run" read at all turn angles.
    constexpr float kTurnThreshold = 0.35f;  // ≈ 20°
    if (isMovementClip(desired) &&
        std::abs(yawDelta(smoothedPlayerYaw_, targetYaw)) > kTurnThreshold) {
      desired = "Idle_Loop";
    }
  }
  if (playerModel_.clipName() != desired) {
    playerModel_.setClip(desired);
  }
  playerModel_.update(dt);

  // Smooth-rotate toward the target yaw via shortest-arc lerp. Pure snap
  // looks like a tank turret rotating instantaneously; a fast exponential
  // ease (half-life ~80ms) reads as "the character pivoted" without
  // visibly lagging server-authoritative facing.
  if (!smoothedYawValid_) {
    smoothedPlayerYaw_ = targetYaw;
    smoothedYawValid_  = true;
  } else {
    constexpr float kTwoPi = 6.28318531f;
    float delta = std::fmod(targetYaw - smoothedPlayerYaw_ + kTwoPi + 3.14159265f,
                            kTwoPi) - 3.14159265f;
    const float k = 1.0f - std::exp(-dt / 0.045f);  // ~45 ms half-life — snappy turn
    smoothedPlayerYaw_ += delta * k;
  }
  glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(fx, yWorld, fy));
  modelMatrix = glm::rotate(modelMatrix, smoothedPlayerYaw_, glm::vec3(0.0f, 1.0f, 0.0f));
  modelMatrix = glm::scale(modelMatrix, glm::vec3(kPlayerScale));

  const glm::vec3 sunDir = sunDirectionFromYawPitch(sunYawDeg_, sunPitchDeg_);
  const glm::mat4 localLightVP = render::ShadowMap::lightViewProj(
      sunDir, followTargetForMap(terrainTileW_, terrainTileH_), shadowHalfExtent_);
  skinnedShader_.use();
  skinnedShader_.setMat4 ("u_viewProj",        viewProj);
  skinnedShader_.setMat4 ("u_lightViewProj",   localLightVP);
  skinnedShader_.setVec3 ("u_lightDir",        sunDir);
  skinnedShader_.setVec3 ("u_paletteLevels",
                          glm::vec3(static_cast<float>(paletteHues_),
                                    static_cast<float>(paletteSats_),
                                    static_cast<float>(paletteLums_)));
  skinnedShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
  skinnedShader_.setFloat("u_ambient",         ambient_);
  skinnedShader_.setFloat("u_diffuse",         diffuse_);
  skinnedShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
  skinnedShader_.setInt  ("u_shadowMap",        1);
  skinnedShader_.setFloat("u_shadowsEnabled",  shadowsEnabled_ ? 1.0f : 0.0f);
  skinnedShader_.setFloat("u_shadowDarkness",  shadowDarkness_);
  skinnedShader_.setFloat("u_shadowBias",      shadowBias_);
  skinnedShader_.setFloat("u_fogEnabled",  fogEnabled_  ? 1.0f : 0.0f);
  skinnedShader_.setVec3 ("u_fogColor",    fogColor_);
  skinnedShader_.setFloat("u_fogDensity",  fogDensity_);
  skinnedShader_.setFloat("u_fogStart",    fogStart_);
  skinnedShader_.setVec3 ("u_color",           kPlayerColor);
  playerModel_.render(skinnedShader_, modelMatrix);
}

// =====================================================================
// Network message dispatch
// =====================================================================

void App::processNetworkMessages() {
  for (const auto& raw : network_.drainMessages()) {
    // Peek the "type" field first, then re-parse as the appropriate struct.
    // shared::MessageHeader lives at namespace scope so glaze's auto-reflection
    // (which requires external linkage) can see it.
    shared::MessageHeader hdr;
    constexpr glz::opts kPermissive{ .error_on_unknown_keys = false };
    if (glz::read<kPermissive>(hdr, raw)) continue;

    if (hdr.type == "init") {
      shared::InitMessage init;
      if (glz::read<kPermissive>(init, raw)) {
        std::fprintf(stderr, "[App] init parse failed\n");
        continue;
      }
      std::fprintf(stdout, "[App] init: player=%s tiles=%dx%d %s\n",
                   init.playerId.c_str(),
                   init.tiles.empty() ? 0 : static_cast<int>(init.tiles[0].size()),
                   static_cast<int>(init.tiles.size()),
                   init.isNewPlayer ? "(new)" : "(returning)");
      isNewPlayer_ = init.isNewPlayer;
      if (isNewPlayer_) joinNameBuf_[0] = '\0';
      // We keep our procedural map; server tiles are acknowledged but ignored.
      currLocalPlayer_.reset();
      prevLocalPlayer_.reset();
    } else if (hdr.type == "state") {
      shared::StateMessage st;
      if (glz::read<kPermissive>(st, raw)) {
        std::fprintf(stderr, "[App] state parse failed\n");
        continue;
      }
      currentTick_  = st.tick;
      npcs_         = std::move(st.npcs);
      droppedItems_ = std::move(st.droppedItems);
      allPlayers_   = st.players;
      // Snapshot rotation for NPC interpolation: previous becomes current,
      // current becomes the just-received state.
      prevNpcs_ = currNpcs_;
      currNpcs_.clear();
      currNpcs_.reserve(npcs_.size());
      for (const auto& n : npcs_) currNpcs_.emplace(n.id, n);
      // Remote player interpolation snapshots + one-shot animation triggers.
      prevRemotePlayers_ = currRemotePlayers_;
      currRemotePlayers_.clear();
      {
        const auto nowRem = std::chrono::steady_clock::now();
        // Helper: compute one-shot deadline from clip name.
        auto remDurMs = [&](const char* name) -> std::chrono::milliseconds {
          const int idx = playerModel_.findClipIndex(name);
          return std::chrono::milliseconds(
            static_cast<int>(playerModel_.clipDuration(idx, 0.6f) * 1000.0f));
        };

        for (const auto& [id, ps] : st.players) {
          if (id == network_.playerId()) continue;
          currRemotePlayers_.emplace(id, ps);

          auto& ra = remoteAnims_[id];
          const auto prevIt = prevRemotePlayers_.find(id);

          // Attack → Sword_Attack (lower priority).
          if (ps.lastAttackTick > ra.seenAttackTick) {
            ra.seenAttackTick = ps.lastAttackTick;
            ra.oneShotClip    = "Sword_Attack";
            ra.oneShotEndsAt  = nowRem + remDurMs("Sword_Attack");
          }
          // Chop → Sword_Attack (overwrites attack if both fire same tick).
          if (ps.lastChopTick > ra.seenChopTick) {
            ra.seenChopTick  = ps.lastChopTick;
            ra.oneShotClip   = "Sword_Attack";
            ra.oneShotEndsAt = nowRem + remDurMs("Sword_Attack");
          }
          // Hit → Hit_Chest (highest one-shot priority; overrides above).
          if (ps.lastHitTick > ra.seenHitTick) {
            ra.seenHitTick = ps.lastHitTick;
            if (ps.lastHitDamage > 0) {
              ra.oneShotClip   = "Hit_Chest";
              ra.oneShotEndsAt = nowRem + remDurMs("Hit_Chest");
            }
          }
          // Pickup completed: pickupItemId just cleared → PickUp_Table.
          const bool pickupNow = ps.pickupItemId.has_value();
          if (ra.prevPickupActive && !pickupNow && ra.oneShotClip.empty()) {
            ra.oneShotClip   = "PickUp_Table";
            ra.oneShotEndsAt = nowRem + remDurMs("PickUp_Table");
          }
          ra.prevPickupActive = pickupNow;
        }
      }
      // Items don't move per tick — snap them.
      entities_.rebuildItems(droppedItems_, map_);

      // Phase 8 — feed chat + hit-splat detectors before we move-from players.
      chatLog_.observePlayers(allPlayers_);
      ui::chatObservePlayers(allPlayers_);
      // System messages from the server (NPC dialogue, "I can't reach that", etc.)
      {
        auto mit = st.messages.find(network_.playerId());
        if (mit != st.messages.end()) {
          for (const auto& msg : mit->second) {
            // Skip raw chat relay entries ("chat:player: text") — those are
            // already surfaced via observePlayers() from chatMessage/chatMessageTick.
            if (msg.size() >= 5 && msg.compare(0, 5, "chat:") == 0) continue;
            chatLog_.appendSystem(msg);
            ui::chatAppendSystem(msg);
          }
        }
      }
      overlays_.update(currentTick_, currLocalPlayer_, npcs_);

      auto it = st.players.find(network_.playerId());
      if (it != st.players.end()) {
        const bool firstState = !currLocalPlayer_.has_value();
        prevLocalPlayer_ = currLocalPlayer_;
        currLocalPlayer_ = it->second;
        lastTickTime_    = std::chrono::steady_clock::now();
        // Phase 5e — per-tick one-shot animation triggers. We only react
        // when the server-authoritative tick stamp moves forward, so
        // late state arrivals or rewinds can't double-fire the clip.
        const auto& cp = *currLocalPlayer_;
        // Helper lambda: resolve clip duration (or fall back to 0.6 s).
        auto oneShotDurMs = [&](const char* name) -> int {
          const int idx = playerModel_.findClipIndex(name);
          return static_cast<int>(playerModel_.clipDuration(idx, 0.6f) * 1000.0f);
        };

        // Attack — Sword_Attack, lower priority than hit.
        if (cp.lastAttackTick > seenAttackTick_) {
          seenAttackTick_ = cp.lastAttackTick;
          oneShotClip_    = "Sword_Attack";
          oneShotEndsAt_  = lastTickTime_ + std::chrono::milliseconds(oneShotDurMs("Sword_Attack"));
          audio_.playStrike();
        }
        // Woodcutting — also plays Sword_Attack (axe swing).
        if (cp.lastChopTick > seenChopTick_) {
          seenChopTick_  = cp.lastChopTick;
          oneShotClip_   = "Sword_Attack";
          oneShotEndsAt_ = lastTickTime_ + std::chrono::milliseconds(oneShotDurMs("Sword_Attack"));
        }
        // Hit / flinch — Hit_Chest overrides attack if both fire same tick.
        if (cp.lastHitTick > seenHitTick_) {
          seenHitTick_ = cp.lastHitTick;
          if (cp.lastHitDamage > 0) {
            audio_.playHit();
            oneShotClip_   = "Hit_Chest";
            oneShotEndsAt_ = lastTickTime_ + std::chrono::milliseconds(oneShotDurMs("Hit_Chest"));
          }
        }
        // Pickup completed: pickupItemId just cleared → play PickUp_Table
        // only if nothing else is already happening this tick.
        const bool pickupActive = cp.pickupItemId.has_value();
        if (prevPickupActive_ && !pickupActive && oneShotClip_.empty()) {
          oneShotClip_   = "PickUp_Table";
          oneShotEndsAt_ = lastTickTime_ + std::chrono::milliseconds(oneShotDurMs("PickUp_Table"));
        }
        prevPickupActive_ = pickupActive;
        // Equip / unequip detection: diff the new equipped map against the
        // last snapshot. New / changed entries -> equip; missing entries
        // -> unequip. First state primes the snapshot silently.
        if (!firstState) {
          for (const auto& [slot, item] : cp.equipped) {
            auto sit = seenEquipped_.find(slot);
            if (sit == seenEquipped_.end() || sit->second != item.itemId) {
              audio_.playEquip();
              break;  // one sound per tick is plenty
            }
          }
          for (const auto& [slot, _] : seenEquipped_) {
            if (cp.equipped.find(slot) == cp.equipped.end()) {
              audio_.playUnequip();
              break;
            }
          }
        }
        seenEquipped_.clear();
        for (const auto& [slot, item] : cp.equipped) {
          seenEquipped_[slot] = item.itemId;
        }
        if (firstState) {
          if (!loginAnnounced_) {
            chatLog_.appendSystem("Welcome to Project L.");
            ui::chatAppendSystem("Welcome to Project L.");
            const std::string loginMsg = std::string("Logged in as ") + network_.playerName() + ".";
            chatLog_.appendSystem(loginMsg);
            ui::chatAppendSystem(loginMsg);
            loginAnnounced_ = true;
          }
          // Reset interpolation/smoothing state — the camera and yaw snap
          // to the new authoritative position instead of easing across
          // the map.
          smoothedYawValid_ = false;
          // Teleport the camera so the player is immediately visible — the
          // server may have placed them well outside our 64x64 procedural
          // map (server constants default PLAYER_START_{X,Y} to 128).
          const glm::vec3 snapTo{
            static_cast<float>(currLocalPlayer_->tileX),
            tileWorldY(map_, currLocalPlayer_->tileX, currLocalPlayer_->tileY) + 1.0f,
            static_cast<float>(currLocalPlayer_->tileY)
          };
          camera_.snapTo(snapTo);
          std::fprintf(stdout, "[App] first state: player at tile (%d, %d)  hp %d/%d\n",
                       currLocalPlayer_->tileX, currLocalPlayer_->tileY,
                       currLocalPlayer_->hp, currLocalPlayer_->maxHp);
        }
      }
    }
  }
}

// =====================================================================
// Login UI panel — shown until status is Connected.
// =====================================================================

bool App::drawLoginUi() {
  if (network_.status() == net::Connection::Connected) return true;

  // Full-screen dark overlay behind the login box.
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::GetBackgroundDrawList()->AddRectFilled(
      ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 180));

  // Centred, fixed-width window, no title bar.
  constexpr float kW = 280.0f;
  ImGui::SetNextWindowSize(ImVec2(kW, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::Begin("##login", nullptr,
      ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings);

  // Title / logo
  {
    const char* title = "Project L";
    const float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((kW - tw) * 0.5f - ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.60f, 0.12f, 1.0f));
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Form — two-column table keeps labels and inputs aligned.
  bool userEnter = false, passEnter = false;
  if (ImGui::BeginTable("##lf", 2)) {
    ImGui::TableSetupColumn("##lc", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("##li", ImGuiTableColumnFlags_WidthStretch);
    const ImVec4 lCol(0.78f, 0.63f, 0.38f, 1.0f);

    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(lCol, "Host");
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##host", loginHost_, sizeof(loginHost_));

    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(lCol, "Port");
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputInt("##port", &loginPort_);

    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(lCol, "Username");
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    userEnter = ImGui::InputText("##user", loginUser_, sizeof(loginUser_),
                                 ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(lCol, "Password");
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
    passEnter = ImGui::InputText("##pass", loginPass_, sizeof(loginPass_),
                                 ImGuiInputTextFlags_Password |
                                 ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::EndTable();
  }

  ImGui::Spacing();
  const auto status = network_.status();
  const bool busy = (status == net::Connection::LoggingIn ||
                     status == net::Connection::Connecting);

  // Trigger connect/register on Enter from either field
  const bool enterPressed = (userEnter || passEnter) && !busy;

  // Login / Register toggle buttons
  {
    const ImVec4 activeCol  (0.55f, 0.34f, 0.14f, 1.0f);
    const ImVec4 inactiveCol(0.28f, 0.17f, 0.07f, 1.0f);
    const float  halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::PushStyleColor(ImGuiCol_Button, loginRegisterMode_ ? inactiveCol : activeCol);
    if (ImGui::Button("Existing User", ImVec2(halfW, 0.0f))) loginRegisterMode_ = false;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, loginRegisterMode_ ? activeCol : inactiveCol);
    if (ImGui::Button("New Account", ImVec2(halfW, 0.0f))) loginRegisterMode_ = true;
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();
  ImGui::BeginDisabled(busy);
  const char* actionLabel = loginRegisterMode_ ? "Create Account" : "Connect";
  if (ImGui::Button(actionLabel, ImVec2(-FLT_MIN, 0.0f)) || enterPressed) {
    if (loginRegisterMode_)
      network_.registerAndConnect(loginHost_, loginPort_, loginUser_, loginPass_);
    else
      network_.loginAndConnect(loginHost_, loginPort_, loginUser_, loginPass_);
  }
  ImGui::EndDisabled();

  // Status text
  switch (status) {
    case net::Connection::Disconnected:
      break;
    case net::Connection::LoggingIn:
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.63f, 0.38f, 1.0f));
      ImGui::TextUnformatted("Authenticating...");
      ImGui::PopStyleColor();
      break;
    case net::Connection::Connecting:
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.63f, 0.38f, 1.0f));
      ImGui::TextUnformatted("Connecting...");
      ImGui::PopStyleColor();
      break;
    case net::Connection::Connected:
      break;
    case net::Connection::Failed:
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
      ImGui::TextWrapped("%s",
          network_.lastError().empty() ? "Connection failed"
                                       : network_.lastError().c_str());
      ImGui::PopStyleColor();
      break;
  }

  ImGui::End();
  return false;
}

// =====================================================================
// PlayerJoinModal — name picker for new accounts
// =====================================================================

void App::saveSettings() {
  AppSettings s;
  s.fogEnabled   = fogEnabled_;   s.fogDensity = fogDensity_;
  s.fogStart     = fogStart_;     s.fogR = fogColor_.r; s.fogG = fogColor_.g; s.fogB = fogColor_.b;
  s.aoEnabled    = aoEnabled_;    s.aoStrength = aoStrength_;
  s.lightingEnabled = lightingEnabled_;
  s.sunYawDeg = sunYawDeg_; s.sunPitchDeg = sunPitchDeg_;
  s.ambient   = ambient_;   s.diffuse     = diffuse_;
  s.shadowsEnabled   = shadowsEnabled_;
  s.shadowDarkness   = shadowDarkness_;
  s.shadowBias       = shadowBias_;
  s.shadowHalfExtent = shadowHalfExtent_;
  s.palette     = palette_;
  s.paletteHues = paletteHues_; s.paletteSats = paletteSats_; s.paletteLums = paletteLums_;
  s.outlineRadius    = outlineRadius_;    s.outlineDepthBias = outlineDepthBias_;
  s.outlineColorR    = outlineColor_.r;   s.outlineColorG = outlineColor_.g;
  s.outlineColorB    = outlineColor_.b;   s.outlineColorA = outlineColor_.a;
  s.hoverTileR = hoverTileColor_.r; s.hoverTileG = hoverTileColor_.g;
  s.hoverTileB = hoverTileColor_.b; s.hoverTileA = hoverTileColor_.a;
  ::saveSettings(s, resolveFromExe("settings.cfg"));
}

void App::loadSettings() {
  // (called from init; exposed as member for future use)
}

void App::drawJoinModal() {
  // Dim the background so the modal draws attention.
  ImGui::GetBackgroundDrawList()->AddRectFilled(
      ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 160));

  const ImGuiIO& io = ImGui::GetIO();
  constexpr float kW = 300.0f;

  ImGui::SetNextWindowSize(ImVec2(kW, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::Begin("##join_modal", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings);

  // Title
  {
    const char* title = "Welcome to Project L!";
    const float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((kW - tw) * 0.5f - ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.65f, 0.15f, 1.0f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();
  ImGui::TextWrapped("Choose a name for your character (up to 12 characters):");
  ImGui::Spacing();

  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##joinname", joinNameBuf_, sizeof(joinNameBuf_),
                   ImGuiInputTextFlags_CallbackCharFilter,
                   [](ImGuiInputTextCallbackData* data) -> int {
                     // Allow alphanumeric and space only; max 12 chars enforced by buf size.
                     if (data->EventChar < 128 &&
                         (std::isalnum(static_cast<unsigned char>(data->EventChar)) ||
                          data->EventChar == ' '))
                       return 0;
                     return 1;  // reject
                   });

  ImGui::Spacing();

  const bool nameOk = (joinNameBuf_[0] != '\0');
  ImGui::BeginDisabled(!nameOk);
  if (ImGui::Button("Confirm", ImVec2(-FLT_MIN, 0.0f))) {
    // Send SET_NAME action to the server.
    char buf[80];
    std::snprintf(buf, sizeof(buf),
                  "{\"type\":\"SET_NAME\",\"playerName\":\"%s\"}", joinNameBuf_);
    network_.sendActionRaw(buf);
    isNewPlayer_ = false;
  }
  ImGui::EndDisabled();

  if (!nameOk) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.5f, 0.3f, 1.0f));
    ImGui::TextUnformatted("Enter a name to continue.");
    ImGui::PopStyleColor();
  }

  ImGui::End();
}

}  // namespace app
