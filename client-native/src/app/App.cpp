#include "app/App.hpp"

#include "render/GlDebug.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>

namespace app {

namespace {
constexpr int kInitialWidth  = 1280;
constexpr int kInitialHeight = 720;
constexpr int kMsaaSamples   = 4;
constexpr const char* kTitle = "OSRS Prototype (native)";
}  // namespace

App::~App() {
  if (imguiInited_) shutdownImGui();
}

bool App::init() {
  if (!window_.init(kInitialWidth, kInitialHeight, kTitle)) return false;

  render::installGlDebugCallback();

  msaa_ = std::make_unique<render::MsaaFramebuffer>(
      window_.framebufferWidth(), window_.framebufferHeight(), kMsaaSamples);

  window_.onFramebufferResize = [this](int w, int h) { onResize(w, h); };

  initImGui();
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

void App::renderFrame() {
  // ---- Main pass into MSAA framebuffer --------------------------------------
  msaa_->bind();
  glClearColor(0.45f, 0.65f, 0.85f, 1.0f);  // sky blue (matches Babylon scene clear)
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // (Future Phases 1+: render terrain, obstacles, entities, indicators here)

  // ---- Resolve to single-sample + blit to window ----------------------------
  msaa_->resolve();
  msaa_->blitToDefault(window_.framebufferWidth(), window_.framebufferHeight());

  // ---- UI pass on default framebuffer ---------------------------------------
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Phase 0 smoke test: show the demo + a tiny status panel.
  ImGui::ShowDemoWindow();

  if (ImGui::Begin("Phase 0 — Bootstrap")) {
    ImGui::Text("GL %s", glGetString(GL_VERSION));
    ImGui::Text("Framebuffer: %d x %d", window_.framebufferWidth(), window_.framebufferHeight());
    ImGui::Text("MSAA samples: %d", msaa_->samples());
    ImGui::Separator();
    ImGui::TextWrapped("Window opens, sky-blue clear color visible through the MSAA pipeline, "
                       "ImGui renders. Phase 0 verified.");
  }
  ImGui::End();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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

}  // namespace app
