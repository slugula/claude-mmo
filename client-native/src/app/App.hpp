#pragma once

#include "app/Window.hpp"
#include "camera/GameCamera.hpp"
#include "input/Picker.hpp"
#include "net/NetworkClient.hpp"
#include "render/Mesh.hpp"
#include "render/MsaaFramebuffer.hpp"
#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"
#include "ui/Panels.hpp"
#include "ui/WorldOverlays.hpp"
#include "world/EntityRenderer.hpp"
#include "world/ObstacleSystem.hpp"
#include "world/SkinnedMesh.hpp"

#include <glad/glad.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace app {

// Top-level application object. Owns the window, GL context, MSAA target,
// scene resources, camera, picker, and drives the render loop.
class App {
public:
  App() = default;
  ~App();

  App(const App&)            = delete;
  App& operator=(const App&) = delete;

  bool init();
  int  run();

private:
  void renderFrame();
  void initImGui();
  void shutdownImGui();
  void onResize(int width, int height);

  void generateAndBuildTerrain();
  void initHoverMesh();
  void destroyHoverMesh();
  void updateHoverMesh(int tx, int ty);

  void renderPlayer(const glm::mat4& viewProj, float dt);
  void processNetworkMessages();
  // Returns true if cursor world position should be sampled for a click
  // action (i.e. a real terrain tile, not an ImGui-owned area).
  bool drawLoginUi();

  Window                                   window_;
  std::unique_ptr<render::MsaaFramebuffer> msaa_;
  render::Shader                           terrainShader_;
  render::Shader                           wireframeShader_;
  render::Shader                           obstacleShader_;
  render::Shader                           skinnedShader_;
  render::Mesh                             terrainMesh_;
  world::ObstacleSystem                    obstacles_;
  world::SkinnedMesh                       playerModel_;
  world::EntityRenderer                    entities_;
  camera::GameCamera                       camera_;

  // Hover indicator — a small dynamic VAO/VBO holding 4 vertices drawn as
  // GL_LINE_LOOP, repositioned each frame to outline the currently
  // hovered tile.
  GLuint                                   hoverVao_     = 0;
  GLuint                                   hoverVbo_     = 0;

  // Networking
  net::NetworkClient                       network_;
  std::optional<shared::PlayerState>       currLocalPlayer_;
  std::optional<shared::PlayerState>       prevLocalPlayer_;
  // All players this tick (local + remote); kept for chat + future overlay
  // expansion. The map is replaced wholesale on each StateMessage.
  std::unordered_map<std::string, shared::PlayerState> allPlayers_;
  std::vector<shared::NPCState>            npcs_;
  std::vector<shared::DroppedItemState>    droppedItems_;
  ui::ChatLog                              chatLog_;
  ui::WorldOverlays                        overlays_;
  bool                                     loginAnnounced_ = false;
  std::chrono::steady_clock::time_point    lastTickTime_{};
  int                                      currentTick_       = 0;
  char                                     loginUser_[64]     = "test";
  char                                     loginPass_[64]     = "test1234";
  char                                     loginHost_[64]     = "localhost";
  int                                      loginPort_         = 8080;

  std::chrono::steady_clock::time_point    lastFrameTime_{};
  shared::WorldMapFile                     map_;
  input::PickResult                        hoveredTile_;

  uint32_t                                 mapSeed_         = 42;
  float                                    noiseFreq_       = 0.04f;
  float                                    noiseAmp_        = 1.0f;
  int                                      terrainTileW_    = 0;
  int                                      terrainTileH_    = 0;
  int                                      terrainIndexCt_  = 0;
  bool                                     wireframe_       = false;
  // Phase 7 — HSL palette quantization (per-fragment).
  bool                                     palette_         = true;
  int                                      paletteHues_     = 64;
  int                                      paletteSats_     = 16;
  int                                      paletteLums_     = 48;
  // Phase 6 — directional lighting. Sun is stored as yaw (0..360°, around
  // world +Y) and pitch (0..90° below horizontal); converted to a unit
  // light-travel-direction each frame.
  bool                                     lightingEnabled_ = true;
  float                                    sunYawDeg_       = 200.0f;
  float                                    sunPitchDeg_     = 58.0f;
  float                                    ambient_         = 0.45f;
  float                                    diffuse_         = 0.55f;
  bool                                     imguiInited_     = false;
};

}  // namespace app
