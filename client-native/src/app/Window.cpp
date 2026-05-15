#include "app/Window.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>

namespace app {

Window::~Window() {
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
}

bool Window::init(int width, int height, const std::string& title) {
  glfwSetErrorCallback([](int code, const char* msg) {
    std::fprintf(stderr, "[GLFW %d] %s\n", code, msg);
  });

  if (!glfwInit()) {
    std::fprintf(stderr, "glfwInit failed\n");
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#if !defined(NDEBUG)
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_SAMPLES, 0);   // we render to our own MSAA FBO

  window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  if (!window_) {
    std::fprintf(stderr, "glfwCreateWindow failed\n");
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);  // vsync on

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::fprintf(stderr, "glad load failed\n");
    return false;
  }

  glfwSetWindowUserPointer(window_, this);
  glfwSetFramebufferSizeCallback(window_, &Window::framebufferSizeCallback);
  glfwSetKeyCallback(window_, &Window::keyCallback);

  glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
  glViewport(0, 0, fbWidth_, fbHeight_);

  std::fprintf(stdout, "GL %s | GLSL %s | %s\n",
               glGetString(GL_VERSION),
               glGetString(GL_SHADING_LANGUAGE_VERSION),
               glGetString(GL_RENDERER));
  return true;
}

bool Window::shouldClose() const {
  return window_ && glfwWindowShouldClose(window_);
}

void Window::pollEvents() {
  glfwPollEvents();
}

void Window::swapBuffers() {
  glfwSwapBuffers(window_);
}

void Window::requestClose() {
  if (window_) glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void Window::framebufferSizeCallback(GLFWwindow* w, int width, int height) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
  if (!self) return;
  self->fbWidth_  = width;
  self->fbHeight_ = height;
  glViewport(0, 0, width, height);
  if (self->onFramebufferResize) self->onFramebufferResize(width, height);
}

void Window::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(w, GLFW_TRUE);
  }
}

}  // namespace app
