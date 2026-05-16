#include "app/App.hpp"

#include "render/GlDebug.hpp"
#include "shared/SharedTypesJson.hpp"
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
constexpr const char* kTitle             = "OSRS Prototype (native)";
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
constexpr const char* kPlayerModelPath   = "assets/models/player.glb";
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
}  // namespace

App::~App() {
  if (imguiInited_) shutdownImGui();
  destroyHoverMesh();
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
  window_.onMouseButton = [this](int button, int action, int /*mods*/) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    camera_.onMouseButton(button, action);
    // Left-click on terrain -> MOVE_TO the hovered tile (Phase 4).
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS
        && hoveredTile_.hit
        && network_.status() == net::Connection::Connected) {
      network_.sendMoveTo(hoveredTile_.tileX, hoveredTile_.tileY);
    }
    // Right-click on world -> Phase 8b-ii context menu. Latch the picked
    // tile so the menu's content stays stable while the cursor moves.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS
        && network_.status() == net::Connection::Connected) {
      ctxMenuRequest_ = true;
      ctxMenuTileHit_ = hoveredTile_.hit;
      if (hoveredTile_.hit) {
        ctxMenuTileX_ = hoveredTile_.tileX;
        ctxMenuTileY_ = hoveredTile_.tileY;
      }
    }
  };
  window_.onScroll = [this](double /*xoffset*/, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    camera_.onScroll(yoffset);
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
  if (!shadowInstancedShader_.fromFiles(resolveFromExe(kShadowInstVertPath),
                                        resolveFromExe(kShadowFragPath))) {
    std::fprintf(stderr, "[App] shadow shader load failed\n");
    return false;
  }
  if (!shadowMap_.init(kShadowMapSize)) {
    std::fprintf(stderr, "[App] shadow map init failed\n");
    return false;
  }

  obstacles_.initGL();
  entities_.initGL();
  generateAndBuildTerrain();
  initHoverMesh();
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
  map_ = world::generateMap(kMapWidth, kMapHeight, mapSeed_, noiseFreq_, noiseAmp_);
  const auto data = world::buildTerrainMesh(map_);
  terrainMesh_.upload(data.positions, data.colors,
                      data.triangleIndices, data.lineIndices,
                      data.normals);
  terrainTileW_   = data.width;
  terrainTileH_   = data.height;
  terrainIndexCt_ = static_cast<int>(data.triangleIndices.size());
  hoveredTile_    = {};  // hover stale after regenerate

  obstacles_.rebuildFromMap(map_);

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

void App::updateHoverMesh(int tx, int ty) {
  // 4 corners of tile (tx, ty) at the current vertex heights.
  // Babylon-convention layout: vertex (row, col) at world (col-0.5, h, H-row-0.5)
  const int   W   = terrainTileW_;
  const int   H   = terrainTileH_;
  const auto& vh  = map_.vertexHeights;
  if (W <= 0 || H <= 0 || vh.empty()) return;

  const float hSW = vh[(H - ty)     * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float hSE = vh[(H - ty)     * (W + 1) + tx + 1] * shared::kMaxTerrainH;
  const float hNW = vh[(H - ty - 1) * (W + 1) + tx]     * shared::kMaxTerrainH;
  const float hNE = vh[(H - ty - 1) * (W + 1) + tx + 1] * shared::kMaxTerrainH;

  // GL_LINE_LOOP visits the 4 vertices in order and closes the loop back to
  // the start. Order SW→SE→NE→NW gives a clean rectangle outline.
  const float verts[12] = {
      tx - 0.5f, hSW, ty - 0.5f,
      tx + 0.5f, hSE, ty - 0.5f,
      tx + 0.5f, hNE, ty + 0.5f,
      tx - 0.5f, hNW, ty + 0.5f,
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
    followTarget = {
      static_cast<float>(currLocalPlayer_->tileX),
      tileWorldY(map_, currLocalPlayer_->tileX, currLocalPlayer_->tileY),
      static_cast<float>(currLocalPlayer_->tileY)
    };
  }
  camera_.update(dt, window_.handle(), followTarget);

  const int   fbW    = window_.framebufferWidth();
  const int   fbH    = window_.framebufferHeight();
  const float aspect = (fbH > 0) ? static_cast<float>(fbW) / static_cast<float>(fbH) : 1.0f;
  const glm::mat4 viewProj = camera_.viewProjection(aspect);

  // ---- Hover pick (skip if ImGui owns the mouse) ----------------------------
  if (!ImGui::GetIO().WantCaptureMouse && fbW > 0 && fbH > 0) {
    glm::vec3 rayOrigin, rayDir;
    input::screenToRay(cursorX, cursorY, fbW, fbH, viewProj, &rayOrigin, &rayDir);
    hoveredTile_ = input::pickTile(rayOrigin, rayDir, map_.vertexHeights,
                                   terrainTileW_, terrainTileH_);
  } else {
    hoveredTile_.hit = false;
  }
  if (hoveredTile_.hit) updateHoverMesh(hoveredTile_.tileX, hoveredTile_.tileY);

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
    shadowInstancedShader_.use();
    shadowInstancedShader_.setMat4("u_lightViewProj", lightVP);
    obstacles_.renderDepth(shadowInstancedShader_);
    shadowMap_.endPass();
  }

  // ---- Main pass into MSAA framebuffer (rebind after the shadow pass) ------
  msaa_->bind();
  glClearColor(0.45f, 0.65f, 0.85f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
  terrainMesh_.draw();

  // ---- Obstacles (instanced) -------------------------------------------------
  obstacleShader_.use();
  obstacleShader_.setMat4 ("u_viewProj",       viewProj);
  obstacleShader_.setVec3 ("u_lightDir",       sunDir);
  obstacleShader_.setVec3 ("u_paletteLevels",
                           glm::vec3(static_cast<float>(paletteHues_),
                                     static_cast<float>(paletteSats_),
                                     static_cast<float>(paletteLums_)));
  obstacleShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
  obstacleShader_.setFloat("u_ambient",         ambient_);
  obstacleShader_.setFloat("u_diffuse",         diffuse_);
  obstacleShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
  obstacles_.render(obstacleShader_);

  // ---- NPCs + dropped items (Phase 5d) ---------------------------------------
  // Reuses the obstacle shader (same uniforms already bound) and per-instance
  // attribute layout. EntityRenderer sets u_color per draw kind internally.
  entities_.render(obstacleShader_);

  // ---- Local player (Phase 5: skinned glTF) ----------------------------------
  processNetworkMessages();
  renderPlayer(viewProj, dt);

  // ---- Wireframe grid overlay ------------------------------------------------
  if (wireframe_) {
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color",    glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    glDepthMask(GL_FALSE);
    terrainMesh_.drawLines();
    glDepthMask(GL_TRUE);
  }

  // ---- Hover tile outline (yellow) ------------------------------------------
  // glLineWidth() with values > 1.0 is not guaranteed in GL 4.6 Core and
  // NVIDIA reports it as GL_INVALID_VALUE ("operation not valid from a
  // preview context"). We stick with the default 1.0 line width; if a
  // thicker outline is needed later we'll expand to a screen-space quad
  // strip in a geometry shader.
  if (hoveredTile_.hit) {
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color",    glm::vec4(1.0f, 0.85f, 0.10f, 1.0f));
    glDepthMask(GL_FALSE);
    glBindVertexArray(hoverVao_);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
  }

  // ---- Resolve to single-sample + blit to window ----------------------------
  msaa_->resolve();
  msaa_->blitToDefault(fbW, fbH);

  // ---- UI pass on default framebuffer ---------------------------------------
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  drawLoginUi();

  // Phase 8a — game UI panels + world overlays. Only worth drawing once
  // we've received a first state from the server (so the panels have
  // something coherent to display).
  if (network_.status() == net::Connection::Connected && currLocalPlayer_) {
    ui::drawSkillsPanel   (*currLocalPlayer_);
    ui::drawInventoryPanel(*currLocalPlayer_, &network_);
    ui::drawEquipmentPanel(*currLocalPlayer_, &network_);
    ui::drawBankPanel     (*currLocalPlayer_, &network_, &bankOpen_);
    chatLog_.draw(&network_);

    overlays_.drawWithHeight(
        viewProj, fbW, fbH,
        currLocalPlayer_, npcs_,
        [this](int tx, int ty) { return tileWorldY(map_, tx, ty); });
  }

  if (ImGui::Begin("Phase 2 - Terrain + Camera")) {
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

  drawWorldContextMenu();

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

  ImGui::Text("Tile (%d, %d)", ctxMenuTileX_, ctxMenuTileY_);
  ImGui::Separator();

  // ---- Tile obstacle ------------------------------------------------------
  shared::ObstacleType obstacle = shared::ObstacleType::none;
  if (ctxMenuTileY_ >= 0 && ctxMenuTileY_ < static_cast<int>(map_.tiles.size()) &&
      ctxMenuTileX_ >= 0 && ctxMenuTileX_ < static_cast<int>(map_.tiles[ctxMenuTileY_].size())) {
    obstacle = map_.tiles[ctxMenuTileY_][ctxMenuTileX_].obstacle;
  }
  switch (obstacle) {
    case shared::ObstacleType::tree:
      if (ImGui::Selectable("Chop tree")) {
        network_.sendChopTree(ctxMenuTileX_, ctxMenuTileY_);
      }
      break;
    case shared::ObstacleType::rock:
      if (ImGui::Selectable("Mine rock")) {
        network_.sendMineRock(ctxMenuTileX_, ctxMenuTileY_);
      }
      break;
    default: break;
  }

  // ---- NPCs at this tile --------------------------------------------------
  for (const auto& n : npcs_) {
    if (n.tileX != ctxMenuTileX_ || n.tileY != ctxMenuTileY_) continue;
    if (n.dying) continue;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Attack %s",
                  n.kind.empty() ? "NPC" : n.kind.c_str());
    if (ImGui::Selectable(buf)) network_.sendAttackNpc(n.id);
    std::snprintf(buf, sizeof(buf), "Talk-to %s",
                  n.kind.empty() ? "NPC" : n.kind.c_str());
    if (ImGui::Selectable(buf)) network_.sendTalkTo(n.id);
  }

  // ---- Dropped items at this tile ----------------------------------------
  for (const auto& it : droppedItems_) {
    if (it.tileX != ctxMenuTileX_ || it.tileY != ctxMenuTileY_) continue;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Take %s",
                  it.itemId.empty() ? "item" : it.itemId.c_str());
    if (ImGui::Selectable(buf)) network_.sendTakeItem(it.id);
  }

  // ---- Always available --------------------------------------------------
  if (ImGui::Selectable("Walk here")) {
    network_.sendMoveTo(ctxMenuTileX_, ctxMenuTileY_);
  }
  // The world doesn't currently render bank chests (no obstacle type for
  // them yet), so this lives at the bottom of every tile's menu as a
  // placeholder. Once chests get a proper obstacle/decoration type we'll
  // gate this on tile.obstacle == bank_chest.
  if (ImGui::Selectable("Open bank")) {
    network_.sendOpenBank();
    bankOpen_ = true;
  }
  ImGui::EndPopup();
}

void App::onResize(int width, int height) {
  if (msaa_) msaa_->resize(width, height);
}

void App::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();
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
//
// State -> animation clip mapping:
//   - default          -> Idle_Loop
//   - is path-walking  -> Walk_Loop
//   - (extend in Phase 10 for combat / death / chop / etc.)
namespace {
// "Walking" iff the server has tiles queued up in `path`. Earlier we also
// checked destinationX/Y != tileX/Y, but the server can leave a non-matching
// destination behind for a tick after the player stops, which made the
// client misread "standing still" as Walk_Loop on first connect. The path
// length is the unambiguous source of truth.
const char* clipForPlayer(const shared::PlayerState* p) {
  if (!p) return "Idle_Loop";
  // Death is sticky — server keeps `dying` true for the full death duration
  // (PLAYER_DEATH_TICKS), so we play Death01 across all those frames.
  if (p->dying)        return "Death01";
  if (p->path.empty()) return "Idle_Loop";
  // Movement at 1 tile per 200ms tick = 5 m/s for a ~1.8m character —
  // that's full sprint territory. Sprint_Loop looks right at that speed;
  // Walk_Loop looks like the character is power-sliding instead.
  return "Sprint_Loop";
}

// Server's PlayerState.facing -> Y-axis rotation in radians.
// World convention: +X = east, +Z = north. glTF rest pose forward is -Z so
// we offset by pi to align "north" with the model's default front. East /
// west swapped relative to the first cut — our left-handed projection flips
// the apparent sense of positive rotation around Y vs the right-handed
// math glm encodes.
float facingToYaw(const std::string& facing) {
  if (facing == "north") return 3.14159265f;          // +pi   -> face +Z
  if (facing == "south") return 0.0f;                  // 0     -> face -Z (model rest)
  if (facing == "east")  return  1.57079632f;          // +pi/2 -> face +X
  if (facing == "west")  return -1.57079632f;          // -pi/2 -> face -X
  return 0.0f;
}
}  // namespace

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

  // Animation clip & timing
  const char* desired = clipForPlayer(&*currLocalPlayer_);
  if (playerModel_.clipName() != desired) {
    playerModel_.setClip(desired);
  }
  playerModel_.update(dt);

  // Build the entity's world transform: translate -> Y-yaw from facing -> scale.
  const float yaw = facingToYaw(currLocalPlayer_->facing);
  glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(fx, yWorld, fy));
  modelMatrix = glm::rotate(modelMatrix, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
  modelMatrix = glm::scale(modelMatrix, glm::vec3(kPlayerScale));

  const glm::vec3 sunDir = sunDirectionFromYawPitch(sunYawDeg_, sunPitchDeg_);
  skinnedShader_.use();
  skinnedShader_.setMat4 ("u_viewProj",        viewProj);
  skinnedShader_.setVec3 ("u_lightDir",        sunDir);
  skinnedShader_.setVec3 ("u_paletteLevels",
                          glm::vec3(static_cast<float>(paletteHues_),
                                    static_cast<float>(paletteSats_),
                                    static_cast<float>(paletteLums_)));
  skinnedShader_.setFloat("u_paletteEnabled",  palette_ ? 1.0f : 0.0f);
  skinnedShader_.setFloat("u_ambient",         ambient_);
  skinnedShader_.setFloat("u_diffuse",         diffuse_);
  skinnedShader_.setFloat("u_lightingEnabled", lightingEnabled_ ? 1.0f : 0.0f);
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
      entities_.rebuildNpcs (npcs_,         map_);
      entities_.rebuildItems(droppedItems_, map_);

      // Phase 8 — feed chat + hit-splat detectors before we move-from players.
      chatLog_.observePlayers(allPlayers_);
      overlays_.update(currentTick_, currLocalPlayer_, npcs_);

      auto it = st.players.find(network_.playerId());
      if (it != st.players.end()) {
        const bool firstState = !currLocalPlayer_.has_value();
        prevLocalPlayer_ = currLocalPlayer_;
        currLocalPlayer_ = it->second;
        lastTickTime_    = std::chrono::steady_clock::now();
        if (firstState) {
          if (!loginAnnounced_) {
            chatLog_.appendSystem("Welcome to Project Reverie.");
            chatLog_.appendSystem(std::string("Logged in as ") + network_.playerName() + ".");
            loginAnnounced_ = true;
          }
          // Teleport the camera so the player is immediately visible — the
          // server may have placed them well outside our 64x64 procedural
          // map (server constants default PLAYER_START_{X,Y} to 128).
          const glm::vec3 snapTo{
            static_cast<float>(currLocalPlayer_->tileX),
            tileWorldY(map_, currLocalPlayer_->tileX, currLocalPlayer_->tileY),
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

  ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(420, FLT_MAX));
  if (ImGui::Begin("Connect to server", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Host",     loginHost_, sizeof(loginHost_));
    ImGui::InputInt ("Port",     &loginPort_);
    ImGui::InputText("Username", loginUser_, sizeof(loginUser_));
    ImGui::InputText("Password", loginPass_, sizeof(loginPass_), ImGuiInputTextFlags_Password);

    const auto status = network_.status();
    const bool busy = (status == net::Connection::LoggingIn || status == net::Connection::Connecting);
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Connect", ImVec2(120, 0))) {
      network_.loginAndConnect(loginHost_, loginPort_, loginUser_, loginPass_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    switch (status) {
      case net::Connection::Disconnected: ImGui::TextUnformatted("Disconnected"); break;
      case net::Connection::LoggingIn:    ImGui::TextUnformatted("Authenticating..."); break;
      case net::Connection::Connecting:   ImGui::TextUnformatted("Connecting to WebSocket..."); break;
      case net::Connection::Connected:    ImGui::TextUnformatted("Connected"); break;
      case net::Connection::Failed:       ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Failed"); break;
    }
    if (status == net::Connection::Failed && !network_.lastError().empty()) {
      ImGui::Separator();
      ImGui::TextWrapped("%s", network_.lastError().c_str());
    }
  }
  ImGui::End();
  return false;
}

}  // namespace app
