#pragma once

#include "app/Window.hpp"
#include "render/MsaaFramebuffer.hpp"

#include <memory>

namespace app {

// Top-level application object. Owns the window, GL context, MSAA target,
// and drives the render loop.
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

  Window                              window_;
  std::unique_ptr<render::MsaaFramebuffer> msaa_;
  bool                                imguiInited_ = false;
};

}  // namespace app
