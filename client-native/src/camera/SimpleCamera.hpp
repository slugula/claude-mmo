#pragma once

#include <glm/glm.hpp>

namespace camera {

// Placeholder for Phase 1: a fixed perspective camera orbiting the map center,
// slowly rotating around the up axis so the unlit vertex-colored terrain is
// visible from all angles without any input plumbing.
//
// Phase 2 replaces this with a full ArcRotateCamera port of GameCamera.ts
// (middle-mouse rotate, wheel zoom, arrow-key rotate, smooth player follow).
class SimpleCamera {
public:
  SimpleCamera() = default;

  // Update camera state. dtSeconds advances the auto-rotate.
  void update(float dtSeconds);

  // Center of orbit (typically the map center on XZ).
  void lookAt(const glm::vec3& target) { target_ = target; }

  // Distance from target, in world units.
  void setRadius(float r) { radius_ = r; }

  // Pitch from horizontal, in radians. Higher = more top-down.
  void setBeta(float b) { beta_ = b; }

  glm::mat4 viewProjection(float aspect) const;

private:
  glm::vec3 target_ = { 32.0f, 0.0f, 32.0f };
  float     radius_ = 40.0f;
  float     alpha_  = -1.0f;   // azimuth, radians
  float     beta_   = 0.9f;    // pitch from horizontal, radians (~51°)
  float     fovY_   = 0.785f;  // 45°
  float     nearZ_  = 0.1f;
  float     farZ_   = 500.0f;
};

}  // namespace camera
