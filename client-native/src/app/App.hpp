#pragma once

#include "app/Window.hpp"
#include "camera/SimpleCamera.hpp"
#include "render/Mesh.hpp"
#include "render/MsaaFramebuffer.hpp"
#include "render/Shader.hpp"
#include "shared/SharedTypes.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

namespace app {

// Top-level application object. Owns the window, GL context, MSAA target,
// scene resources, and drives the render loop.
class App {
public:
  App() = default;
  ~App();

  App(const App&)            = delete;
  App& operator=(const App&) = delete;

  // One-time init. Returns false on failure.
  bool init();

  // Blocks until window is closed. Returns the process exit code.
  int run();

private:
  void renderFrame();
  void initImGui();
  void shutdownImGui();
  void onResize(int width, int height);

  void generateAndBuildTerrain();

  Window                                   window_;
  std::unique_ptr<render::MsaaFramebuffer> msaa_;
  render::Shader                           terrainShader_;
  render::Shader                           wireframeShader_;
  render::Mesh                             terrainMesh_;
  camera::SimpleCamera                     camera_;
  std::chrono::steady_clock::time_point    lastFrameTime_{};
  shared::WorldMapFile                     map_;
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
  int                                      paletteLums_     = 16;
  bool                                     imguiInited_     = false;
};

}  // namespace app
