#include "camera/SimpleCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace camera {

void SimpleCamera::update(float dtSeconds) {
  alpha_ += dtSeconds * 0.15f;  // ~one full rotation every ~42 s
}

glm::mat4 SimpleCamera::viewProjection(float aspect) const {
  const float cosB = std::cos(beta_);
  const float sinB = std::sin(beta_);
  const glm::vec3 eye = target_ + glm::vec3{
      radius_ * cosB * std::sin(alpha_),
      radius_ * sinB,
      radius_ * cosB * std::cos(alpha_)
  };
  const glm::mat4 view = glm::lookAtLH(eye, target_, glm::vec3{0.0f, 1.0f, 0.0f});
  const glm::mat4 proj = glm::perspectiveLH(fovY_, aspect, nearZ_, farZ_);
  return proj * view;
}

}  // namespace camera
