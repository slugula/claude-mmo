#include "camera/GameCamera.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace camera {

namespace {

// Constants mirror src/shared/constants.ts. Radius range is widened for the
// native client's larger default view (Phase 2 has no player yet; the user
// orbits the whole map). Reduce when we put a player at the centre.
constexpr float kMinRadius        = 5.0f;
constexpr float kMaxRadius        = 120.0f;
constexpr float kMinBeta          = 0.392699f;   //  π/8   (~22°, nearly top-down)
constexpr float kMaxBeta          = 1.428000f;   //  π/2.2 (~82°, near horizontal)
constexpr float kRotateSpeed      = 1.2f;        // radians / second when arrow held
constexpr float kZoomSpeed        = 2.5f;        // world units per scroll tick
constexpr float kDragSensitivity  = 0.005f;      // radians per pixel
constexpr float kFollowSpeed      = 10.0f;       // larger = snappier follow
// If the follow target jumps farther than this in one update (login, teleport),
// snap the look-at point instead of easing — so logging in places the camera on
// the player with no pan. Normal walking moves ≤ a couple tiles/tick.
constexpr float kTeleportDist2    = 8.0f * 8.0f; // squared tiles

}  // namespace

void GameCamera::onMouseButton(int button, int action) {
  if (button != GLFW_MOUSE_BUTTON_MIDDLE) return;
  if (action == GLFW_PRESS) {
    dragging_      = true;
    dragHasOrigin_ = false;  // next onCursorPos primes lastX/lastY without moving
  } else if (action == GLFW_RELEASE) {
    dragging_      = false;
  }
}

void GameCamera::onCursorPos(double x, double y) {
  if (dragging_) {
    if (!dragHasOrigin_) {
      // First sample after press — just record where we are, no motion yet.
      dragHasOrigin_ = true;
    } else {
      const double dx = x - lastX_;
      const double dy = y - lastY_;
      // Inverted: drag-right rotates camera right (alpha increases)
      targetAlpha_ += static_cast<float>(dx) * kDragSensitivity;
      // Inverted: drag-up tips camera down (lower beta = more top-down)
      targetBeta_ = std::clamp(
          targetBeta_ + static_cast<float>(dy) * kDragSensitivity,
          kMinBeta, kMaxBeta);
    }
  }
  lastX_ = x;
  lastY_ = y;
}

void GameCamera::onScroll(double yoffset) {
  // Scroll up = zoom IN = reduce radius. yoffset is positive when scrolling up.
  targetRadius_ = std::clamp(targetRadius_ - static_cast<float>(yoffset) * kZoomSpeed,
                             kMinRadius, kMaxRadius);
}

void GameCamera::update(float dt, GLFWwindow* w, const glm::vec3& target) {
  // Arrow-key rotation
  const float rotStep = kRotateSpeed * dt;
  if (w) {
    if (glfwGetKey(w, GLFW_KEY_LEFT)  == GLFW_PRESS) targetAlpha_ += rotStep;
    if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS) targetAlpha_ -= rotStep;
    if (glfwGetKey(w, GLFW_KEY_UP)    == GLFW_PRESS)
      targetBeta_ = std::clamp(targetBeta_ + rotStep, kMinBeta, kMaxBeta);
    if (glfwGetKey(w, GLFW_KEY_DOWN)  == GLFW_PRESS)
      targetBeta_ = std::clamp(targetBeta_ - rotStep, kMinBeta, kMaxBeta);
  }

  // Smooth alpha / beta / radius (exponential decay toward target).
  const float snap = 1.0f - std::pow(0.001f, dt);
  alpha_  += (targetAlpha_  - alpha_)  * snap;
  beta_   += (targetBeta_   - beta_)   * snap;
  radius_ += (targetRadius_ - radius_) * snap;

  // Smooth target follow — the camera's look-at point chases the requested
  // world position. With kFollowSpeed = 10 it never lags far behind even if
  // a future Phase-4 player teleports.
  const float followT = 1.0f - std::exp(-kFollowSpeed * dt);
  targetPos_ = target;
  const glm::vec3 d = targetPos_ - currentTarget_;
  if ((d.x * d.x + d.z * d.z) > kTeleportDist2) {
    currentTarget_ = targetPos_;   // big jump (login / teleport) → snap, no pan
  } else {
    currentTarget_.x += d.x * followT;
    currentTarget_.y += d.y * followT;
    currentTarget_.z += d.z * followT;
  }
}

glm::vec3 GameCamera::cameraPosition() const {
  const float cosB = std::cos(beta_);
  const float sinB = std::sin(beta_);
  return currentTarget_ + glm::vec3{
      radius_ * cosB * std::sin(alpha_),
      radius_ * sinB,
      radius_ * cosB * std::cos(alpha_)
  };
}

glm::mat4 GameCamera::viewProjection(float aspect) const {
  const glm::vec3 eye  = cameraPosition();
  const glm::mat4 view = glm::lookAtLH(eye, currentTarget_, glm::vec3{0.0f, 1.0f, 0.0f});
  const glm::mat4 proj = glm::perspectiveLH(0.785398f /*45°*/, aspect, 0.1f, 500.0f);
  return proj * view;
}

void GameCamera::snapTo(const glm::vec3& target) {
  targetPos_     = target;
  currentTarget_ = target;
}

void GameCamera::pan(float right, float forward) {
  // Camera yaw is alpha_ (azimuth). Forward direction in XZ:
  //   forward = ( sin(alpha), 0, cos(alpha) )  — same convention as cameraPosition()
  const float sinA = std::sin(alpha_);
  const float cosA = std::cos(alpha_);
  // Forward in world XZ (toward target from camera position projected to XZ)
  const glm::vec3 fwd { sinA, 0.0f, cosA };
  // Right is perpendicular in XZ
  const glm::vec3 rgt { cosA, 0.0f, -sinA };
  targetPos_ += rgt * right + fwd * forward;
}

}  // namespace camera
