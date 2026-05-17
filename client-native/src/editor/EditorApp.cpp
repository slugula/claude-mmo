#include "editor/EditorApp.hpp"

#include "editor/EditorPalette.hpp"
#include "input/Picker.hpp"
#include "render/GlDebug.hpp"
#include "shared/SharedTypesJson.hpp"
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
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace editor {

namespace {

constexpr int   kInitialWidth  = 1440;
constexpr int   kInitialHeight = 900;
constexpr int   kMsaaSamples   = 4;
constexpr const char* kTitle             = "OSRS Level Editor";
constexpr const char* kTerrainVertPath   = "shaders/terrain.vert";
constexpr const char* kTerrainFragPath   = "shaders/terrain.frag";
constexpr const char* kWireframeVertPath = "shaders/wireframe.vert";
constexpr const char* kWireframeFragPath = "shaders/wireframe.frag";
constexpr const char* kObstacleVertPath  = "shaders/obstacle.vert";
constexpr const char* kObstacleFragPath  = "shaders/obstacle.frag";
constexpr const char* kOutlineVertPath   = "shaders/outline.vert";
constexpr const char* kOutlineFragPath   = "shaders/outline.frag";
constexpr const char* kShadowInstVertPath= "shaders/shadow_instanced.vert";
constexpr const char* kShadowFragPath    = "shaders/shadow.frag";
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

// Standard slab-method ray-vs-AABB.
float rayVsAABB(glm::vec3 ro, glm::vec3 rd, glm::vec3 bMin, glm::vec3 bMax) {
  float tMin = 1e-4f, tMax = 1e30f;
  for (int i = 0; i < 3; ++i) {
    if (std::abs(rd[i]) < 1e-8f) {
      if (ro[i] < bMin[i] || ro[i] > bMax[i]) return -1.0f;
      continue;
    }
    float t0 = (bMin[i] - ro[i]) / rd[i];
    float t1 = (bMax[i] - ro[i]) / rd[i];
    if (t0 > t1) std::swap(t0, t1);
    tMin = std::max(tMin, t0);
    tMax = std::min(tMax, t1);
    if (tMax < tMin) return -1.0f;
  }
  return tMin;
}

// Default tile colour for blank maps.
constexpr const char* kDefaultGroundColor = "#4a7c2a";

} // namespace

// -----------------------------------------------------------------------
EditorApp::~EditorApp() {
  if (imguiInited_) shutdownImGui();
  destroyHoverMesh();
}

// -----------------------------------------------------------------------
bool EditorApp::init() {
  if (!window_.init(kInitialWidth, kInitialHeight, kTitle)) return false;

  render::installGlDebugCallback();

  // 3D viewport FBO (not the window FBO).
  viewport3dFbo_ = std::make_unique<render::MsaaFramebuffer>(
      viewport3dW_, viewport3dH_, kMsaaSamples);

  // ---- Window callbacks ------------------------------------------------
  window_.onFramebufferResize = [this](int /*w*/, int /*h*/) {};  // window FBO unused

  window_.onMouseButton = [this](int button, int action, int /*mods*/) {
    if (ImGui::GetIO().WantCaptureMouse) {
      // Still handle 3D-viewport click if the viewport window captures it.
      // We rely on mouseHeld3D_ set by drawToolbar/render3DViewport logic.
      if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS)   mouseHeld3D_ = true;
        if (action == GLFW_RELEASE) mouseHeld3D_ = false;
      }
      return;
    }
    // Camera drag (middle button)
    camera_.onMouseButton(button, action);
  };

  window_.onScroll = [this](double /*xoffset*/, double yoffset) {
    // If hovering 2D grid, adjust zoom.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) return;
    camera_.onScroll(yoffset);
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
  if (!loadShader(terrainShader_,          kTerrainVertPath,    kTerrainFragPath,    "terrain"))    return false;
  if (!loadShader(wireframeShader_,        kWireframeVertPath,  kWireframeFragPath,  "wireframe"))  return false;
  if (!loadShader(obstacleShader_,         kObstacleVertPath,   kObstacleFragPath,   "obstacle"))   return false;
  if (!loadShader(outlineShader_,          kOutlineVertPath,    kOutlineFragPath,    "outline"))    return false;
  if (!loadShader(shadowInstancedShader_,  kShadowInstVertPath, kShadowFragPath,     "shadow"))     return false;

  if (!shadowMap_.init(kShadowMapSize)) {
    std::fprintf(stderr, "[Editor] shadow map init failed\n");
    return false;
  }

  obstacles_.initGL();
  if (!obstacles_.loadTreeModel(resolveFromExe(kTreeModelPath))) {
    std::fprintf(stderr, "[Editor] tree model not found — using procedural trees\n");
  }
  entities_.initGL();

  // ---- Initial blank map ------------------------------------------------
  initNewMap(64, 64);

  // ---- Camera + GL state -----------------------------------------------
  camera_.snapTo({ static_cast<float>(map_.width) * 0.5f, 0.0f,
                   static_cast<float>(map_.height) * 0.5f });

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);
  glDisable(GL_CULL_FACE);

  initHoverMesh();
  initImGui();

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
  // Keyboard shortcuts
  {
    GLFWwindow* w = window_.handle();
    const bool ctrl  = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS
                    || glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    const bool shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT)    == GLFW_PRESS;
    static bool sKeyPressedZ = false, sKeyPressedY = false, sKeyPressedS = false;

    // Ctrl+Z undo
    const bool zNow = (ctrl && glfwGetKey(w, GLFW_KEY_Z) == GLFW_PRESS);
    if (zNow && !sKeyPressedZ && undo_.canUndo()) {
      const auto& snap = undo_.undo();
      map_       = snap.map;
      npcSpawns_ = snap.npcs;
      rebuildTerrainGL();
      rebuildObstacles();
      minimap_.rebuild(map_, npcSpawns_);
    }
    sKeyPressedZ = zNow;

    // Ctrl+Y redo
    const bool yNow = (ctrl && glfwGetKey(w, GLFW_KEY_Y) == GLFW_PRESS);
    if (yNow && !sKeyPressedY && undo_.canRedo()) {
      const auto& snap = undo_.redo();
      map_       = snap.map;
      npcSpawns_ = snap.npcs;
      rebuildTerrainGL();
      rebuildObstacles();
      minimap_.rebuild(map_, npcSpawns_);
    }
    sKeyPressedY = yNow;

    // Ctrl+S save
    const bool sNow = (ctrl && !shift && glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS);
    if (sNow && !sKeyPressedS) saveCurrentFile();
    sKeyPressedS = sNow;
  }

  // Camera cursor update
  double cursorX = 0.0, cursorY = 0.0;
  glfwGetCursorPos(window_.handle(), &cursorX, &cursorY);
  camera_.onMouseButton(GLFW_MOUSE_BUTTON_MIDDLE, -1); // no-op, just update state
  camera_.onCursorPos(cursorX, cursorY);
  camera_.update(dt, window_.handle(),
                 { static_cast<float>(map_.width) * 0.5f, 0.0f,
                   static_cast<float>(map_.height) * 0.5f });

  // Pending undo push (after brush stroke ends)
  if (undoPending_ && !hadStroke_) {
    undoPending_ = false;
    pushUndo();
  }

  // ---- Render 3D scene to viewport FBO ---------------------------------
  render3DViewport(dt);

  // ---- ImGui -----------------------------------------------------------
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  drawMenuBar();
  drawToolbar();
  drawProperties();
  drawGridView();
  drawMinimapWindow();

  // ---- New map dialog --------------------------------------------------
  if (showNewMapDialog_) {
    ImGui::OpenPopup("New Map");
    showNewMapDialog_ = false;
  }
  if (ImGui::BeginPopupModal("New Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Map size:");
    ImGui::InputInt("Width##nm",  &newMapW_);
    ImGui::InputInt("Height##nm", &newMapH_);
    newMapW_ = std::clamp(newMapW_, 8, 256);
    newMapH_ = std::clamp(newMapH_, 8, 256);
    if (ImGui::Button("Create", ImVec2(80, 0))) {
      initNewMap(newMapW_, newMapH_);
      currentFilePath_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ---- Resize dialog ---------------------------------------------------
  if (showResizeDialog_) {
    ImGui::OpenPopup("Resize Map");
    showResizeDialog_ = false;
  }
  if (ImGui::BeginPopupModal("Resize Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("New size (crop/pad):");
    ImGui::InputInt("Width##rs",  &resizeW_);
    ImGui::InputInt("Height##rs", &resizeH_);
    resizeW_ = std::clamp(resizeW_, 8, 256);
    resizeH_ = std::clamp(resizeH_, 8, 256);
    if (ImGui::Button("Apply", ImVec2(80, 0))) {
      resizeMap(resizeW_, resizeH_);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ---- Blit everything to the window -----------------------------------
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

  // Shadow pass
  const glm::vec3 mapCenter = { static_cast<float>(map_.width) * 0.5f, 0.0f,
                                 static_cast<float>(map_.height) * 0.5f };
  const glm::mat4 lightVP = render::ShadowMap::lightViewProj(sunDir, mapCenter, shadowHalfExtent_);
  if (shadowsEnabled_) {
    shadowMap_.beginPass();
    shadowInstancedShader_.use();
    shadowInstancedShader_.setMat4("u_lightViewProj", lightVP);
    obstacles_.renderDepth(shadowInstancedShader_);
    shadowMap_.endPass();
  }

  // Main pass into viewport FBO
  viewport3dFbo_->bind();
  glViewport(0, 0, viewport3dW_, viewport3dH_);
  glClearColor(0.45f, 0.65f, 0.85f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
  terrainMesh_.draw();

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
  obstacles_.render(obstacleShader_);

  // NPC stand-ins (editor-side npcSpawns_)
  {
    std::vector<world::EntityRenderer::Instance> insts;
    insts.reserve(npcSpawns_.size());
    for (const auto& n : npcSpawns_) {
      const float wy = tileWorldY(n.tileX, n.tileY);
      insts.push_back({ static_cast<float>(n.tileX), wy, static_cast<float>(n.tileY), 0.0f });
    }
    entities_.setNpcInstances(insts);
    entities_.render(obstacleShader_);
  }

  // Hover outline (yellow quad in 3D)
  if (hoveredTileX_ >= 0) {
    updateHoverMesh(hoveredTileX_, hoveredTileY_,
                    brush_.size, brush_.size);
    wireframeShader_.use();
    wireframeShader_.setMat4("u_viewProj", viewProj);
    wireframeShader_.setVec4("u_color",   glm::vec4(1.0f, 0.85f, 0.10f, 1.0f));
    glDepthMask(GL_FALSE);
    glBindVertexArray(hoverVao_);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
  }

  // Obstacle outline on hovered tile
  if (hoveredTileX_ >= 0 &&
      hoveredTileY_ < static_cast<int>(map_.tiles.size()) &&
      hoveredTileX_ < static_cast<int>(map_.tiles[hoveredTileY_].size())) {
    const auto obs = map_.tiles[hoveredTileY_][hoveredTileX_].obstacle;
    if (obs != shared::ObstacleType::none) {
      outlineShader_.use();
      outlineShader_.setMat4("u_viewProj",     viewProj);
      outlineShader_.setVec4("u_outlineColor", glm::vec4(0.0f, 0.9f, 0.9f, 0.8f));
      obstacles_.renderOutlineAt(outlineShader_, map_, hoveredTileX_, hoveredTileY_);
    }
  }

  viewport3dFbo_->resolve();
  (void)dt;
}

// -----------------------------------------------------------------------
void EditorApp::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Map...", "Ctrl+N")) { showNewMapDialog_ = true; }
    if (ImGui::MenuItem("Open...",   "Ctrl+O")) { openFileDialog();  }
    ImGui::Separator();
    if (ImGui::MenuItem("Save",      "Ctrl+S")) { saveCurrentFile(); }
    if (ImGui::MenuItem("Save As...")) { saveAsDialog();  }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit"))
      glfwSetWindowShouldClose(window_.handle(), GLFW_TRUE);
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Map")) {
    if (ImGui::MenuItem("Resize...")) {
      resizeW_ = map_.width;
      resizeH_ = map_.height;
      showResizeDialog_ = true;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Height Overlay",      nullptr, &showHeightOverlay_);
    ImGui::MenuItem("Walkability Overlay", nullptr, &showWalkabilityOverlay_);
    ImGui::MenuItem("Gridmap Overlay",     nullptr, &showGridmapOverlay_);
    ImGui::Separator();
    ImGui::MenuItem("Palette Quantisation",nullptr, &palette_);
    ImGui::MenuItem("Lighting",            nullptr, &lightingEnabled_);
    ImGui::MenuItem("Shadows",             nullptr, &shadowsEnabled_);
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

// -----------------------------------------------------------------------
void EditorApp::drawToolbar() {
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus;

  const float menuH   = ImGui::GetFrameHeight();
  const float toolW   = 120.0f;
  const float winH    = static_cast<float>(window_.framebufferHeight()) - menuH;

  ImGui::SetNextWindowPos(ImVec2(0, menuH));
  ImGui::SetNextWindowSize(ImVec2(toolW, winH));
  ImGui::Begin("##toolbar", nullptr, flags | ImGuiWindowFlags_NoTitleBar);

  auto toolBtn = [&](const char* label, EditorTool t) {
    const bool active = (activeTool_ == t);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
    if (ImGui::Button(label, ImVec2(-1, 0))) activeTool_ = t;
    if (active) ImGui::PopStyleColor();
  };

  ImGui::TextDisabled("-- Tools --");
  toolBtn("Paint",     EditorTool::PaintTerrain);
  toolBtn("Raise",     EditorTool::SculptRaise);
  toolBtn("Lower",     EditorTool::SculptLower);
  toolBtn("Obstacle",  EditorTool::PlaceObstacle);
  toolBtn("NPC",       EditorTool::PlaceNPC);
  toolBtn("Spawn",     EditorTool::PlaceSpawn);
  toolBtn("Walkable",  EditorTool::PaintWalkable);
  toolBtn("Blocked",   EditorTool::PaintBlocked);
  toolBtn("Erase",     EditorTool::Erase);

  ImGui::Separator();
  ImGui::TextDisabled("-- Brush --");

  ImGui::SetNextItemWidth(-1);
  ImGui::SliderInt("##sz", &brush_.size, 1, 32, "Size:%d");
  brush_.size = std::clamp(brush_.size, 1, 64);

  bool isRound = (brush_.shape == BrushShape::Round);
  if (ImGui::Checkbox("Round", &isRound))
    brush_.shape = isRound ? BrushShape::Round : BrushShape::Square;

  const bool isSculpt = (activeTool_ == EditorTool::SculptRaise ||
                          activeTool_ == EditorTool::SculptLower);
  if (isSculpt) {
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##str", &brush_.strength, 0.01f, 0.5f, "Str:%.2f");
  }

  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::drawProperties() {
  const float menuH = ImGui::GetFrameHeight();
  const float propW = 200.0f;
  const float toolW = 120.0f;
  const float winH  = static_cast<float>(window_.framebufferHeight()) - menuH;
  const float winW  = static_cast<float>(window_.framebufferWidth());

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGui::SetNextWindowPos(ImVec2(winW - propW, menuH));
  ImGui::SetNextWindowSize(ImVec2(propW, winH));
  ImGui::Begin("Properties##props", nullptr, flags);

  // ---- Tool-specific controls ------------------------------------------
  if (activeTool_ == EditorTool::PaintTerrain) {
    ImGui::TextDisabled("Palette");

    float col[3] = { paletteR_, paletteG_, paletteB_ };

    // 32-swatch grid (4 columns)
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
                             ImGuiColorEditFlags_NoTooltip |
                             ImGuiColorEditFlags_NoBorder,
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
    ImGui::TextDisabled("Obstacle type");
    auto obstBtn = [&](const char* label, shared::ObstacleType t) {
      const bool a = (obstacleSubtype_ == t);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
      if (ImGui::Button(label, ImVec2(-1, 0))) obstacleSubtype_ = t;
      if (a) ImGui::PopStyleColor();
    };
    obstBtn("Tree",  shared::ObstacleType::tree);
    obstBtn("Rock",  shared::ObstacleType::rock);
    obstBtn("Chest", shared::ObstacleType::chest);
    obstBtn("Fence", shared::ObstacleType::fence);
  }
  else if (activeTool_ == EditorTool::PlaceNPC) {
    ImGui::TextDisabled("NPC type");
    auto npcBtn = [&](const char* label) {
      const bool a = (npcSubtype_ == label);
      if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.34f, 0.10f, 1.0f));
      if (ImGui::Button(label, ImVec2(-1, 0))) npcSubtype_ = label;
      if (a) ImGui::PopStyleColor();
    };
    npcBtn("chicken");
    npcBtn("shopkeeper");
  }

  // ---- Lighting -------------------------------------------------------
  ImGui::Separator();
  ImGui::TextDisabled("Lighting");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##yaw",   &sunYawDeg_,   0.0f, 360.0f, "Yaw:%.0f");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##pitch", &sunPitchDeg_, 10.0f,  90.0f, "Pitch:%.0f");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##amb",   &ambient_,      0.0f,   1.0f, "Amb:%.2f");
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##diff",  &diffuse_,      0.0f,   1.0f, "Diff:%.2f");

  // ---- File I/O -------------------------------------------------------
  ImGui::Separator();
  ImGui::TextDisabled("File");
  if (ImGui::Button("New Map",  ImVec2(-1, 0))) showNewMapDialog_ = true;
  if (ImGui::Button("Open...",  ImVec2(-1, 0))) openFileDialog();
  if (ImGui::Button("Save",     ImVec2(-1, 0))) saveCurrentFile();
  if (ImGui::Button("Save As",  ImVec2(-1, 0))) saveAsDialog();

  // ---- Undo/Redo -------------------------------------------------------
  ImGui::Separator();
  if (!undo_.canUndo()) ImGui::BeginDisabled();
  if (ImGui::Button("Undo  (Ctrl+Z)", ImVec2(-1, 0)) && undo_.canUndo()) {
    const auto& snap = undo_.undo();
    map_ = snap.map; npcSpawns_ = snap.npcs;
    rebuildTerrainGL(); rebuildObstacles();
    minimap_.rebuild(map_, npcSpawns_);
  }
  if (!undo_.canUndo()) ImGui::EndDisabled();

  if (!undo_.canRedo()) ImGui::BeginDisabled();
  if (ImGui::Button("Redo  (Ctrl+Y)", ImVec2(-1, 0)) && undo_.canRedo()) {
    const auto& snap = undo_.redo();
    map_ = snap.map; npcSpawns_ = snap.npcs;
    rebuildTerrainGL(); rebuildObstacles();
    minimap_.rebuild(map_, npcSpawns_);
  }
  if (!undo_.canRedo()) ImGui::EndDisabled();

  // ---- Current file path -----------------------------------------------
  ImGui::Separator();
  if (currentFilePath_.empty())
    ImGui::TextDisabled("(unsaved)");
  else {
    const auto p = std::filesystem::path(currentFilePath_).filename();
    ImGui::TextWrapped("%s", p.string().c_str());
  }

  ImGui::End();
}

// -----------------------------------------------------------------------
void EditorApp::drawGridView() {
  const float menuH = ImGui::GetFrameHeight();
  const float toolW = 120.0f;
  const float propW = 200.0f;
  const float winW  = static_cast<float>(window_.framebufferWidth());
  const float winH  = static_cast<float>(window_.framebufferHeight()) - menuH;
  // Bottom half, between toolbar and properties
  const float gridX = toolW;
  const float gridW = winW - toolW - propW;
  const float gridH = winH * 0.35f;
  const float gridY = menuH + winH - gridH;

  ImGui::SetNextWindowPos(ImVec2(gridX, gridY));
  ImGui::SetNextWindowSize(ImVec2(gridW, gridH));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_HorizontalScrollbar;
  ImGui::Begin("2D Grid##grid", nullptr, flags);

  ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  ImDrawList* dl    = ImGui::GetWindowDrawList();

  // Mouse scroll in grid = zoom
  if (ImGui::IsWindowHovered()) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      gridZoom_ = std::clamp(gridZoom_ * (wheel > 0 ? 1.15f : (1.0f / 1.15f)), 2.0f, 32.0f);
    }
    // Middle-mouse drag = pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
      gridOffX_ += delta.x;
      gridOffY_ += delta.y;
    }
  }

  const float z  = gridZoom_;
  const int   W  = map_.width;
  const int   H  = map_.height;

  // Clamp draw range to visible region
  const int x0 = std::max(0, static_cast<int>((-gridOffX_) / z));
  const int y0 = std::max(0, static_cast<int>((-gridOffY_) / z));
  const int x1 = std::min(W, static_cast<int>((-gridOffX_ + canvasSize.x) / z) + 2);
  const int y1 = std::min(H, static_cast<int>((-gridOffY_ + canvasSize.y) / z) + 2);

  for (int ty = y0; ty < y1; ++ty) {
    if (ty >= static_cast<int>(map_.tiles.size())) break;
    for (int tx = x0; tx < x1; ++tx) {
      if (tx >= static_cast<int>(map_.tiles[ty].size())) break;
      const auto& tile = map_.tiles[ty][tx];

      float fr = 0.29f, fg = 0.49f, fb = 0.16f;
      hexToRgbf(tile.groundColor.c_str(), fr, fg, fb);

      if (showHeightOverlay_) {
        // Average vertex height at tile corners
        const int vW = W + 1;
        const int vH = H + 1;
        const auto& vh = map_.vertexHeights;
        float h = 0.0f;
        if (!vh.empty() && static_cast<int>(vh.size()) >= (vH * vW)) {
          h += vh[(H - ty)     * vW + tx];
          h += vh[(H - ty)     * vW + tx + 1];
          h += vh[(H - ty - 1) * vW + tx];
          h += vh[(H - ty - 1) * vW + tx + 1];
          h *= 0.25f;
        }
        const float g = std::clamp(h, 0.0f, 1.0f);
        fr = fr * 0.5f + g * 0.5f;
        fg = fg * 0.5f + g * 0.5f;
        fb = fb * 0.5f + g * 0.5f;
      }

      const float px = canvasPos.x + gridOffX_ + tx * z;
      const float py = canvasPos.y + gridOffY_ + ty * z;
      dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z),
                        IM_COL32(static_cast<int>(fr * 255), static_cast<int>(fg * 255),
                                  static_cast<int>(fb * 255), 255));

      // Walkability overlay
      if (showWalkabilityOverlay_ && !tile.walkable) {
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z),
                          IM_COL32(220, 30, 30, 90));
      }
      if (showGridmapOverlay_) {
        const ImU32 col = tile.walkable ? IM_COL32(0, 200, 0, 60) : IM_COL32(200, 0, 0, 60);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z), col);
      }

      // Obstacle icon (colour dot if tile is large enough)
      if (z >= 6.0f && tile.obstacle != shared::ObstacleType::none) {
        ImU32 oc = IM_COL32(20, 90, 10, 255);
        if (tile.obstacle == shared::ObstacleType::rock)  oc = IM_COL32(110, 110, 110, 255);
        if (tile.obstacle == shared::ObstacleType::chest) oc = IM_COL32(200, 160, 30, 255);
        if (tile.obstacle == shared::ObstacleType::fence) oc = IM_COL32(100, 60, 20, 255);
        const float r = std::max(2.0f, z * 0.28f);
        dl->AddCircleFilled(ImVec2(px + z * 0.5f, py + z * 0.5f), r, oc);
      }

      // Grid lines if zoomed in enough
      if (z >= 6.0f) {
        dl->AddRect(ImVec2(px, py), ImVec2(px + z, py + z), IM_COL32(0, 0, 0, 40));
      }
    }
  }

  // NPC markers
  for (const auto& n : npcSpawns_) {
    const float px = canvasPos.x + gridOffX_ + n.tileX * z + z * 0.5f;
    const float py = canvasPos.y + gridOffY_ + n.tileY * z + z * 0.5f;
    const ImU32 nc = (n.kind == "shopkeeper") ? IM_COL32(180, 50, 220, 255) : IM_COL32(255, 220, 0, 255);
    dl->AddCircleFilled(ImVec2(px, py), std::max(2.0f, z * 0.25f), nc);
  }

  // Spawn point marker (white cross)
  {
    const int sx = map_.spawnPoint[0];
    const int sy = map_.spawnPoint[1];
    const float px = canvasPos.x + gridOffX_ + sx * z + z * 0.5f;
    const float py = canvasPos.y + gridOffY_ + sy * z + z * 0.5f;
    const float arm = std::max(3.0f, z * 0.4f);
    dl->AddLine(ImVec2(px - arm, py), ImVec2(px + arm, py), IM_COL32(255, 255, 255, 230), 2.0f);
    dl->AddLine(ImVec2(px, py - arm), ImVec2(px, py + arm), IM_COL32(255, 255, 255, 230), 2.0f);
  }

  // Hover indicator
  if (hoveredTileX_ >= 0 && z >= 2.0f) {
    const float px = canvasPos.x + gridOffX_ + hoveredTileX_ * z;
    const float py = canvasPos.y + gridOffY_ + hoveredTileY_ * z;
    const float s  = brush_.size * z;
    dl->AddRect(ImVec2(px, py), ImVec2(px + s, py + s), IM_COL32(255, 220, 30, 180), 0.0f, 0, 1.5f);
  }

  // Mouse interaction in grid
  if (ImGui::IsWindowHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    const auto mp = ImGui::GetMousePos();
    const int tx = static_cast<int>((mp.x - canvasPos.x - gridOffX_) / z);
    const int ty = static_cast<int>((mp.y - canvasPos.y - gridOffY_) / z);

    if (tx >= 0 && tx < W && ty >= 0 && ty < H) {
      hoveredTileX_ = tx;
      hoveredTileY_ = ty;

      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!mouseHeldGrid_) {
          mouseHeldGrid_ = true;
          if (!undoPending_) pushUndo();
          undoPending_ = true;
        }
        applyBrush(tx, ty, 0.016f);
        hadStroke_ = true;
      } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        // Right-click erases in grid
        if (!mouseHeldGrid_) {
          mouseHeldGrid_ = true;
          if (!undoPending_) pushUndo();
          undoPending_ = true;
        }
        // Erase obstacle / NPC at tile
        if (ty < static_cast<int>(map_.tiles.size()) &&
            tx < static_cast<int>(map_.tiles[ty].size())) {
          setObstacleAtTile(tx, ty, shared::ObstacleType::none);
          npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
            [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
            npcSpawns_.end());
          rebuildObstacles();
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
  const float menuH = ImGui::GetFrameHeight();
  const float toolW = 120.0f;
  const float winW  = static_cast<float>(window_.framebufferWidth());
  const float winH  = static_cast<float>(window_.framebufferHeight()) - menuH;
  const float propW = 200.0f;

  // 3D viewport occupies everything between toolbar and properties, top section
  const float gridH  = winH * 0.35f;
  const float vpArea = winW - toolW - propW;
  const float vpH    = winH - gridH;

  // 3D viewport window (displays the FBO texture)
  ImGui::SetNextWindowPos(ImVec2(toolW, menuH));
  ImGui::SetNextWindowSize(ImVec2(vpArea, vpH));
  const ImGuiWindowFlags vpFlags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoScrollbar;
  ImGui::Begin("3D Viewport##3dvp", nullptr, vpFlags);

  // Resize FBO if needed
  {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int nw = std::max(4, static_cast<int>(avail.x));
    const int nh = std::max(4, static_cast<int>(avail.y));
    if (nw != viewport3dW_ || nh != viewport3dH_) {
      viewport3dW_ = nw;
      viewport3dH_ = nh;
      viewport3dFbo_->resize(nw, nh);
    }
  }

  // Display resolved FBO texture
  const GLuint tex = viewport3dFbo_->resolveColorTexture();
  // ImGui images use top-left origin; GL textures bottom-left → flip V
  ImGui::Image((ImTextureID)(uintptr_t)(tex),
               ImVec2(static_cast<float>(viewport3dW_), static_cast<float>(viewport3dH_)),
               ImVec2(0, 1), ImVec2(1, 0));

  // 3D viewport mouse picking
  if (ImGui::IsItemHovered()) {
    const auto mp   = ImGui::GetMousePos();
    const auto iPos = ImGui::GetItemRectMin();
    const float px  = mp.x - iPos.x;
    const float py  = mp.y - iPos.y;

    const float aspect = (viewport3dH_ > 0)
      ? static_cast<float>(viewport3dW_) / static_cast<float>(viewport3dH_)
      : 1.0f;
    const glm::mat4 vp = camera_.viewProjection(aspect);
    glm::vec3 ro, rd;
    input::screenToRay(px, py, viewport3dW_, viewport3dH_, vp, &ro, &rd);

    // Pick terrain
    const auto pick = input::pickTile(ro, rd, map_.vertexHeights, map_.width, map_.height);
    if (pick.hit) {
      hoveredTileX_ = pick.tileX;
      hoveredTileY_ = pick.tileY;
    }

    // Camera input in 3D viewport
    const auto& io = ImGui::GetIO();
    // Middle-mouse drag → camera rotation
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
      camera_.onMouseButton(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS);
    }
    // Scroll → zoom
    if (io.MouseWheel != 0.0f) {
      camera_.onScroll(static_cast<double>(io.MouseWheel));
    }

    // Left-click to apply tool
    if (pick.hit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (!mouseHeld3D_) {
        mouseHeld3D_ = true;
        if (!undoPending_) pushUndo();
        undoPending_ = true;
      }
      applyBrush(pick.tileX, pick.tileY, ImGui::GetIO().DeltaTime);
      hadStroke_ = true;
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      mouseHeld3D_ = false;
    }
  } else {
    mouseHeld3D_ = false;
  }

  ImGui::End();

  // ---- Minimap window (float, top-right corner of 3D area) ------------
  const float mmSz = 180.0f;
  ImGui::SetNextWindowPos(ImVec2(winW - propW - mmSz - 8.0f, menuH + 8.0f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(mmSz + 16.0f, mmSz + 32.0f), ImGuiCond_Always);
  ImGui::Begin("Minimap##mm",  nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoScrollbar);
  const GLuint mmTex = minimap_.texture();
  if (mmTex) {
    ImGui::Image((ImTextureID)(uintptr_t)(mmTex),
                 ImVec2(mmSz, mmSz),
                 ImVec2(0, 0), ImVec2(1, 1));
  } else {
    ImGui::TextDisabled("(no minimap)");
  }
  ImGui::End();
}

// -----------------------------------------------------------------------
// Brush dispatch: apply the active tool at every tile within brush radius.
void EditorApp::applyBrush(int cx, int cy, float dt) {
  const int half = brush_.size / 2;
  const float r  = static_cast<float>(half);

  for (int dy = -half; dy <= half; ++dy) {
    for (int dx = -half; dx <= half; ++dx) {
      if (brush_.shape == BrushShape::Round) {
        const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        if (d > r + 0.5f) continue;
      }
      applyToolAt(cx + dx, cy + dy, dt);
    }
  }
}

// -----------------------------------------------------------------------
void EditorApp::applyToolAt(int tx, int ty, float dt) {
  if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) return;
  if (ty >= static_cast<int>(map_.tiles.size()))   return;
  if (tx >= static_cast<int>(map_.tiles[ty].size())) return;

  auto& tile = map_.tiles[ty][tx];

  switch (activeTool_) {
    case EditorTool::PaintTerrain: {
      tile.groundColor = rgbfToHex(paletteR_, paletteG_, paletteB_);
      // Incremental GPU update for just this tile's vertices
      repaintVertexColors(tx, ty, tx + 1, ty + 1);
      minimap_.rebuild(map_, npcSpawns_);
      break;
    }

    case EditorTool::SculptRaise:
    case EditorTool::SculptLower: {
      const float dir = (activeTool_ == EditorTool::SculptRaise) ? 1.0f : -1.0f;
      const int W = map_.width;
      const int H = map_.height;
      auto& vh = map_.vertexHeights;
      if (vh.empty()) break;

      // Gaussian weight for sculpt brush
      const float half = static_cast<float>(brush_.size) * 0.5f;
      auto gaussW = [&](int vx, int vy) -> float {
        // vertex (vx, vy) in the heightmap corresponds to tile position (vx-0.5, vy-0.5)
        const float dx = vx - (tx + 0.5f);
        const float dy = vy - (ty + 0.5f);
        const float d  = std::sqrt(dx * dx + dy * dy);
        const float sig = half + 0.5f;
        return std::exp(-(d * d) / (2.0f * sig * sig));
      };

      // The 4 corners of tile (tx, ty) in vertex grid are:
      // Row (H - ty - 1) to (H - ty), col tx to tx+1
      for (int vrow = H - ty - 1; vrow <= H - ty; ++vrow) {
        for (int vcol = tx; vcol <= tx + 1; ++vcol) {
          if (vrow < 0 || vrow > H || vcol < 0 || vcol > W) continue;
          const int idx = vrow * (W + 1) + vcol;
          const float w = gaussW(vcol, H - vrow);
          vh[static_cast<std::size_t>(idx)] = std::clamp(
            vh[static_cast<std::size_t>(idx)] + dir * brush_.strength * w * dt,
            0.0f, 1.0f);
        }
      }

      resculptNormals(tx, ty, tx + 1, ty + 1);
      break;
    }

    case EditorTool::PlaceObstacle: {
      setObstacleAtTile(tx, ty, obstacleSubtype_);
      rebuildObstacles();
      minimap_.rebuild(map_, npcSpawns_);
      break;
    }

    case EditorTool::PlaceNPC: {
      // Remove existing NPC at tile, then add new one
      npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
        [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
        npcSpawns_.end());
      shared::NpcSpawn ns;
      ns.kind  = npcSubtype_;
      ns.tileX = tx;
      ns.tileY = ty;
      npcSpawns_.push_back(ns);
      minimap_.rebuild(map_, npcSpawns_);
      break;
    }

    case EditorTool::PlaceSpawn: {
      map_.spawnPoint = { tx, ty };
      minimap_.rebuild(map_, npcSpawns_);
      break;
    }

    case EditorTool::PaintWalkable: {
      tile.walkable = true;
      break;
    }

    case EditorTool::PaintBlocked: {
      tile.walkable = false;
      break;
    }

    case EditorTool::Erase: {
      // Erase obstacle
      setObstacleAtTile(tx, ty, shared::ObstacleType::none);
      tile.walkable = true;
      // Erase NPC at tile
      npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
        [tx, ty](const shared::NpcSpawn& n){ return n.tileX == tx && n.tileY == ty; }),
        npcSpawns_.end());
      rebuildObstacles();
      minimap_.rebuild(map_, npcSpawns_);
      break;
    }
  }
}

// -----------------------------------------------------------------------
void EditorApp::repaintVertexColors(int x0, int y0, int x1, int y1) {
  // Full mesh rebuild is cheap for paint; incremental SubData isn't yet
  // plumbed through TerrainBuilder, so we just do a full rebuild.
  rebuildTerrainGL();
}

void EditorApp::resculptNormals(int /*x0*/, int /*y0*/, int /*x1*/, int /*y1*/) {
  // Rebuild full mesh (positions + normals) after sculpting.
  rebuildTerrainGL();
}

// -----------------------------------------------------------------------
void EditorApp::initNewMap(int w, int h) {
  map_ = {};
  map_.width  = w;
  map_.height = h;
  map_.spawnPoint = { w / 2, h / 2 };

  map_.tiles.assign(static_cast<std::size_t>(h),
                    std::vector<shared::TileData>(static_cast<std::size_t>(w)));
  for (int ty = 0; ty < h; ++ty) {
    for (int tx = 0; tx < w; ++tx) {
      auto& t = map_.tiles[ty][tx];
      t.x           = tx;
      t.y           = ty;
      t.walkable    = true;
      t.groundColor = kDefaultGroundColor;
      t.type        = shared::TileType::grass;
      t.obstacle    = shared::ObstacleType::none;
      t.blocksRanged= false;
      t.height      = 0.0f;
    }
  }

  // Flat heightmap
  map_.vertexHeights.assign(static_cast<std::size_t>((w + 1) * (h + 1)), 0.0f);

  rebuildTerrainGL();
  rebuildObstacles();
  npcSpawns_.clear();
  undo_.clear();
  pushUndo();  // initial state
  minimap_.init(w, h);
  minimap_.rebuild(map_, npcSpawns_);

  camera_.snapTo({ static_cast<float>(w) * 0.5f, 0.0f,
                   static_cast<float>(h) * 0.5f });
}

// -----------------------------------------------------------------------
void EditorApp::rebuildTerrainGL() {
  terrainData_ = world::buildTerrainMesh(map_);
  terrainMesh_.upload(terrainData_.positions, terrainData_.colors,
                      terrainData_.triangleIndices, terrainData_.lineIndices,
                      terrainData_.normals);
}

void EditorApp::rebuildObstacles() {
  obstacles_.rebuildFromMap(map_);
}

// -----------------------------------------------------------------------
void EditorApp::setObstacleAtTile(int tx, int ty, shared::ObstacleType obs) {
  if (ty < 0 || ty >= static_cast<int>(map_.tiles.size())) return;
  if (tx < 0 || tx >= static_cast<int>(map_.tiles[ty].size())) return;
  auto& tile    = map_.tiles[ty][tx];
  tile.obstacle = obs;

  // Auto-set walkability
  if (obs == shared::ObstacleType::none) {
    tile.walkable     = true;
    tile.blocksRanged = false;
  } else if (obs == shared::ObstacleType::fence) {
    tile.walkable     = false;
    tile.blocksRanged = false;
  } else {
    tile.walkable     = false;
    tile.blocksRanged = true;
  }
}

// -----------------------------------------------------------------------
float EditorApp::tileWorldY(int tx, int ty) const {
  const int W = map_.width;
  const int H = map_.height;
  if (W <= 0 || H <= 0 || tx < 0 || ty < 0 || tx >= W || ty >= H) return 0.0f;
  const auto& vh = map_.vertexHeights;
  if (static_cast<int>(vh.size()) != (W + 1) * (H + 1)) return 0.0f;
  const float SW = vh[static_cast<std::size_t>((H - ty)     * (W + 1) + tx)]     * shared::kMaxTerrainH;
  const float SE = vh[static_cast<std::size_t>((H - ty)     * (W + 1) + tx + 1)] * shared::kMaxTerrainH;
  const float NW = vh[static_cast<std::size_t>((H - ty - 1) * (W + 1) + tx)]     * shared::kMaxTerrainH;
  const float NE = vh[static_cast<std::size_t>((H - ty - 1) * (W + 1) + tx + 1)] * shared::kMaxTerrainH;
  return (SW + SE + NW + NE) * 0.25f;
}

int EditorApp::clampTile(int v, int max) const {
  return std::clamp(v, 0, max - 1);
}

// -----------------------------------------------------------------------
// Hover mesh (yellow quad outline around hovered tile/brush area)
void EditorApp::initHoverMesh() {
  destroyHoverMesh();
  glCreateVertexArrays(1, &hoverVao_);
  glCreateBuffers(1, &hoverVbo_);
  glNamedBufferStorage(hoverVbo_, sizeof(float) * 3 * 4, nullptr, GL_DYNAMIC_STORAGE_BIT);
  glVertexArrayVertexBuffer(hoverVao_, 0, hoverVbo_, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(hoverVao_, 0);
  glVertexArrayAttribFormat(hoverVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(hoverVao_, 0, 0);
}

void EditorApp::destroyHoverMesh() {
  if (hoverVbo_) { glDeleteBuffers(1, &hoverVbo_); hoverVbo_ = 0; }
  if (hoverVao_) { glDeleteVertexArrays(1, &hoverVao_); hoverVao_ = 0; }
}

void EditorApp::updateHoverMesh(int tx, int ty, int szX, int szY) {
  const int W = map_.width;
  const int H = map_.height;
  const auto& vh = map_.vertexHeights;
  if (W <= 0 || H <= 0 || vh.empty()) return;

  const int tx2 = std::min(tx + szX - 1, W - 1);
  const int ty2 = std::min(ty + szY - 1, H - 1);

  auto safeVH = [&](int row, int col) -> float {
    row = std::clamp(row, 0, H);
    col = std::clamp(col, 0, W);
    return vh[static_cast<std::size_t>(row * (W + 1) + col)] * shared::kMaxTerrainH;
  };

  const float hSW = safeVH(H - ty,      tx);
  const float hSE = safeVH(H - ty,      tx2 + 1);
  const float hNE = safeVH(H - ty2 - 1, tx2 + 1);
  const float hNW = safeVH(H - ty2 - 1, tx);

  const float verts[12] = {
    tx  - 0.5f, hSW + 0.05f, ty  - 0.5f,
    tx2 + 0.5f, hSE + 0.05f, ty  - 0.5f,
    tx2 + 0.5f, hNE + 0.05f, ty2 + 0.5f,
    tx  - 0.5f, hNW + 0.05f, ty2 + 0.5f,
  };
  glNamedBufferSubData(hoverVbo_, 0, sizeof(verts), verts);
}

// -----------------------------------------------------------------------
void EditorApp::resizeMap(int newW, int newH) {
  pushUndo();

  // Rebuild tiles array
  std::vector<std::vector<shared::TileData>> newTiles(
    static_cast<std::size_t>(newH),
    std::vector<shared::TileData>(static_cast<std::size_t>(newW)));

  for (int ty = 0; ty < newH; ++ty) {
    for (int tx = 0; tx < newW; ++tx) {
      if (ty < map_.height && tx < map_.width) {
        newTiles[ty][tx] = map_.tiles[ty][tx];
      } else {
        auto& t = newTiles[ty][tx];
        t.x           = tx;
        t.y           = ty;
        t.walkable    = true;
        t.groundColor = kDefaultGroundColor;
        t.type        = shared::TileType::grass;
        t.obstacle    = shared::ObstacleType::none;
        t.blocksRanged= false;
        t.height      = 0.0f;
      }
    }
  }
  map_.tiles = std::move(newTiles);

  // Rebuild vertex heights array (crop/pad)
  const int oldVW = map_.width  + 1;
  const int oldVH = map_.height + 1;
  const int newVW = newW + 1;
  const int newVH = newH + 1;
  std::vector<float> newVH_arr(static_cast<std::size_t>(newVW * newVH), 0.0f);
  for (int vr = 0; vr < newVH; ++vr) {
    for (int vc = 0; vc < newVW; ++vc) {
      // Map new vertex row to old: old height origin is bottom-up, same as new
      if (vr < oldVH && vc < oldVW && !map_.vertexHeights.empty()) {
        const int oldIdx = vr * oldVW + vc;
        newVH_arr[static_cast<std::size_t>(vr * newVW + vc)] =
          map_.vertexHeights[static_cast<std::size_t>(oldIdx)];
      }
    }
  }
  map_.vertexHeights = std::move(newVH_arr);
  map_.width  = newW;
  map_.height = newH;
  map_.spawnPoint = { std::min(map_.spawnPoint[0], newW - 1),
                      std::min(map_.spawnPoint[1], newH - 1) };

  // Remove NPC spawns outside new bounds
  npcSpawns_.erase(std::remove_if(npcSpawns_.begin(), npcSpawns_.end(),
    [newW, newH](const shared::NpcSpawn& n){
      return n.tileX >= newW || n.tileY >= newH;
    }), npcSpawns_.end());

  rebuildTerrainGL();
  rebuildObstacles();
  minimap_.init(newW, newH);
  minimap_.rebuild(map_, npcSpawns_);
}

// -----------------------------------------------------------------------
void EditorApp::pushUndo() {
  undo_.push(map_, npcSpawns_);
}

// -----------------------------------------------------------------------
void EditorApp::newMapDialog() {
  showNewMapDialog_ = true;
}

void EditorApp::openFileDialog() {
  const std::wstring path = winOpenDialog();
  if (path.empty()) return;

  shared::WorldMapFile loaded;
  if (!shared::loadWorldMap(std::filesystem::path(path), loaded)) {
    std::fprintf(stderr, "[Editor] Failed to load map\n");
    return;
  }
  pushUndo();
  map_ = std::move(loaded);
  // npcSpawns are now stored inside WorldMapFile
  npcSpawns_ = map_.npcSpawns;
  currentFilePath_ = std::filesystem::path(path).string();

  rebuildTerrainGL();
  rebuildObstacles();
  minimap_.init(map_.width, map_.height);
  minimap_.rebuild(map_, npcSpawns_);
  camera_.snapTo({ static_cast<float>(map_.width) * 0.5f, 0.0f,
                   static_cast<float>(map_.height) * 0.5f });
}

void EditorApp::saveCurrentFile() {
  if (currentFilePath_.empty()) {
    saveAsDialog();
    return;
  }
  // Copy npcSpawns into map before saving
  map_.npcSpawns = npcSpawns_;
  if (!shared::saveWorldMap(std::filesystem::path(currentFilePath_), map_)) {
    std::fprintf(stderr, "[Editor] Save failed\n");
  }
}

void EditorApp::saveAsDialog() {
  const std::wstring path = winSaveDialog();
  if (path.empty()) return;
  currentFilePath_ = std::filesystem::path(path).string();
  saveCurrentFile();
}

// -----------------------------------------------------------------------
std::wstring EditorApp::winOpenDialog() {
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn     = {};
  ofn.lStructSize       = sizeof(ofn);
  ofn.hwndOwner         = nullptr;
  ofn.lpstrFilter       = L"JSON Map (*.json)\0*.json\0All Files\0*.*\0";
  ofn.lpstrFile         = buf;
  ofn.nMaxFile          = MAX_PATH;
  ofn.Flags             = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  ofn.lpstrDefExt       = L"json";
  if (GetOpenFileNameW(&ofn)) return buf;
  return {};
}

std::wstring EditorApp::winSaveDialog() {
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn     = {};
  ofn.lStructSize       = sizeof(ofn);
  ofn.hwndOwner         = nullptr;
  ofn.lpstrFilter       = L"JSON Map (*.json)\0*.json\0All Files\0*.*\0";
  ofn.lpstrFile         = buf;
  ofn.nMaxFile          = MAX_PATH;
  ofn.Flags             = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  ofn.lpstrDefExt       = L"json";
  if (GetSaveFileNameW(&ofn)) return buf;
  return {};
}

// -----------------------------------------------------------------------
void EditorApp::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  const auto fontPath = resolveFromExe("assets/ProggyClean.ttf");
  if (std::filesystem::exists(fontPath)) {
    io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 13.0f);
  } else {
    io.Fonts->AddFontDefault();
  }

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
  c[ImGuiCol_Text]                  = ImVec4(0.94f, 0.82f, 0.50f, 1.00f);
  c[ImGuiCol_TextDisabled]          = ImVec4(0.54f, 0.44f, 0.25f, 1.00f);
  c[ImGuiCol_WindowBg]              = ImVec4(0.11f, 0.07f, 0.03f, 0.97f);
  c[ImGuiCol_ChildBg]               = ImVec4(0.09f, 0.06f, 0.02f, 0.80f);
  c[ImGuiCol_PopupBg]               = ImVec4(0.10f, 0.06f, 0.02f, 0.97f);
  c[ImGuiCol_Border]                = ImVec4(0.42f, 0.31f, 0.16f, 0.90f);
  c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_FrameBg]               = ImVec4(0.07f, 0.04f, 0.01f, 0.90f);
  c[ImGuiCol_FrameBgHovered]        = ImVec4(0.15f, 0.09f, 0.03f, 0.90f);
  c[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.12f, 0.04f, 1.00f);
  c[ImGuiCol_TitleBg]               = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_TitleBgActive]         = ImVec4(0.28f, 0.17f, 0.07f, 1.00f);
  c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.11f, 0.07f, 0.03f, 0.90f);
  c[ImGuiCol_MenuBarBg]             = ImVec4(0.18f, 0.11f, 0.04f, 1.00f);
  c[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.03f, 0.01f, 0.80f);
  c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.42f, 0.31f, 0.16f, 0.90f);
  c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.55f, 0.40f, 0.20f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.70f, 0.52f, 0.25f, 1.00f);
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

}  // namespace editor
