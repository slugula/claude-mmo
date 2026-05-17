#pragma once

// Forward-declare so this header doesn't drag <glad/glad.h> + <GLFW/glfw3.h>
// into everywhere it's included.
struct GLFWwindow;

#include <functional>
#include <string>

namespace app {

// Owns a GLFW window + OpenGL 4.6 Core context.
//
// Construct, then call init() once. The constructor doesn't initialize so we
// can return a clean error code from init() without throwing.
class Window {
public:
  Window() = default;
  ~Window();

  Window(const Window&)            = delete;
  Window& operator=(const Window&) = delete;

  // Create a window with a 4.6 Core context. Returns false on failure.
  bool init(int width, int height, const std::string& title);

  bool shouldClose() const;
  void pollEvents();
  void swapBuffers();

  // Current framebuffer size in pixels (may differ from window size on HiDPI).
  int framebufferWidth()  const { return fbWidth_;  }
  int framebufferHeight() const { return fbHeight_; }

  GLFWwindow* handle() const { return window_; }

  // Set to false to request the loop exit.
  void requestClose();

  // Callbacks fired from pollEvents().
  std::function<void(int width, int height)>                  onFramebufferResize;
  // Mouse button press / release. button = GLFW_MOUSE_BUTTON_*, action =
  // GLFW_PRESS or GLFW_RELEASE, mods = GLFW_MOD_* bitfield.
  std::function<void(int button, int action, int mods)>       onMouseButton;
  // Scroll wheel — yoffset is the primary axis (positive = scroll up).
  std::function<void(double xoffset, double yoffset)>         onScroll;
  // Keyboard key press/release.
  std::function<void(int key, int action, int mods)>          onKey;
  // Unicode character input (for text entry).
  std::function<void(unsigned int codepoint)>                 onChar;

private:
  GLFWwindow* window_   = nullptr;
  int         fbWidth_  = 0;
  int         fbHeight_ = 0;

  static void framebufferSizeCallback(GLFWwindow* w, int width, int height);
  static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
  static void charCallback(GLFWwindow* w, unsigned int codepoint);
  static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
  static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset);
};

}  // namespace app
