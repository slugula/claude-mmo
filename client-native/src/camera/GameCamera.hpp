#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace camera {

// ArcRotate camera with smooth alpha / beta / radius / target follow.
//
// Port of src/camera/GameCamera.ts (the browser client's camera). Controls:
//   - Middle-mouse drag : free rotate (alpha + beta)
//   - Mouse wheel       : zoom (radius)
//   - Arrow keys        : keyboard rotate (alpha + beta)
//   - update(target)    : the camera's look-at point smoothly chases the
//                         passed-in world position (Phase 4 will pass the
//                         player's world pos here; Phase 2 just uses the
//                         map centre)
//
// The class is callback-friendly:
//   - onMouseButton(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS/RELEASE)
//   - onCursorPos(x, y) — call every frame with the current cursor position
//                         (we read drag-deltas from this; no GLFW capture)
//   - onScroll(yoff)    — wheel ticks
//   - update(dtSeconds, glfwWindow, target) — call once per frame; reads
//                         arrow-key state via glfwGetKey and smooths
//                         alpha/beta/radius/position toward their targets.
class GameCamera {
public:
  GameCamera() = default;

  // Wire from Window callbacks
  void onMouseButton(int button, int action);
  // Call once per frame with the current cursor pos. Drag deltas are computed
  // from the previous-frame position. The first call after each press just
  // initializes lastX/lastY without moving the camera.
  void onCursorPos(double x, double y);
  void onScroll(double yoffset);

  // Per-frame tick. Reads arrow keys via glfwGetKey; smooths state toward
  // its targets; advances the look-at point toward `target`.
  void update(float dtSeconds, GLFWwindow* w, const glm::vec3& target);

  // Built from current smoothed state.
  glm::mat4 viewProjection(float aspect) const;
  glm::vec3 cameraPosition() const;
  glm::vec3 lookAtTarget()   const { return currentTarget_; }
  glm::vec3 panTarget()      const { return targetPos_; }  // the unsmoothed target; pass back into update() to avoid overwrite

  // Reset alpha/beta/radius to defaults and snap target to a position.
  void snapTo(const glm::vec3& target);

  // Pan the camera target in world-space XZ by a camera-relative offset
  // (forward/right relative to the camera's current yaw). Call each frame
  // with a velocity * dt value.
  void pan(float right, float forward);

  bool isDragging() const { return dragging_; }

private:
  // ---- Smoothed targets (where we want to be) -----------------------------
  float targetAlpha_  = -0.785398f;   // -π/4
  float targetBeta_   =  0.897598f;   //  π/3.5
  float targetRadius_ = 40.0f;
  glm::vec3 targetPos_{32.0f, 0.0f, 32.0f};

  // ---- Current smoothed state ---------------------------------------------
  float alpha_  = -0.785398f;
  float beta_   =  0.897598f;
  float radius_ = 40.0f;
  glm::vec3 currentTarget_{32.0f, 0.0f, 32.0f};

  // ---- Drag state ---------------------------------------------------------
  bool   dragging_       = false;
  bool   dragHasOrigin_  = false;
  double lastX_          = 0.0;
  double lastY_          = 0.0;
};

}  // namespace camera
