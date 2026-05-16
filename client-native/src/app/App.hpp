#pragma once

#include "app/Window.hpp"
#include "camera/GameCamera.hpp"
#include "input/Picker.hpp"
#include "render/Mesh.hpp"
#include "render/MsaaFramebuffer.hpp"
#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <glad/glad.h>

#include <chrono>
#include <cstdint>
#include <memory>

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

  Window                                   window_;
  std::unique_ptr<render::MsaaFramebuffer> msaa_;
  render::Shader                           terrainShader_;
  render::Shader                           wireframeShader_;
  render::Mesh                             terrainMesh_;
  camera::GameCamera                       camera_;

  // Hover indicator — a small dynamic VAO/VBO holding 4 vertices drawn as
  // GL_LINE_LOOP, repositioned each frame to outline the currently
  // hovered tile.
  GLuint                                   hoverVao_     = 0;
  GLuint                                   hoverVbo_     = 0;

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
  bool                                     imguiInited_     = false;
};

}  // namespace app
