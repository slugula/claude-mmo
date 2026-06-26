#pragma once

// AttachmentRenderer — draws small static glTF models (equipped weapons) at an
// arbitrary world matrix, e.g. parented to a player's hand-bone socket. Models
// are loaded on demand and cached by their (relative) path. Reuses the existing
// single-model `preview` shader (u_model + u_viewProj + u_color, VAO layout
// pos@0 / normal@1 / color@4). Shared by the game client and the editor.

#include "render/Shader.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace world {

// Build a grip offset matrix: translate * (Rz*Ry*Rx, degrees) * uniform scale.
// Shared by client + editor so the in-hand transform is computed identically.
glm::mat4 gripMatrix(const glm::vec3& posOffset,
                     const glm::vec3& rotEulerDeg,
                     float scale);

class AttachmentRenderer {
public:
  AttachmentRenderer()  = default;
  ~AttachmentRenderer() { destroy(); }

  AttachmentRenderer(const AttachmentRenderer&)            = delete;
  AttachmentRenderer& operator=(const AttachmentRenderer&) = delete;

  // Load the preview shader. `resolver` turns a model's relative path
  // (e.g. "assets/models/sword.glb") into an absolute path next to the exe.
  bool init(const std::string& vertPath, const std::string& fragPath,
            std::function<std::filesystem::path(const std::string&)> resolver);
  void destroy();

  bool valid() const { return shader_.isValid(); }

  // Draw the model at `relPath` (cached) at `world`. No-op if it fails to load
  // or the path is empty. Expects depth test already enabled by the caller.
  void draw(const std::string& relPath,
            const glm::mat4& world, const glm::mat4& viewProj);

private:
  struct Prim {
    GLuint    vao = 0, vboPos = 0, vboNrm = 0, vboCol = 0, vboUv = 0, ebo = 0;
    GLuint    texture = 0;   // baseColorTexture; 0 = none
    GLsizei   indexCount = 0;
    glm::vec4 color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
  };
  struct Model {
    std::vector<Prim> prims;
    bool              attempted = false;  // load tried (avoid retrying failures)
  };

  const Model& ensure(const std::string& relPath);

  render::Shader shader_;
  std::function<std::filesystem::path(const std::string&)> resolver_;
  std::unordered_map<std::string, Model> cache_;
};

}  // namespace world
