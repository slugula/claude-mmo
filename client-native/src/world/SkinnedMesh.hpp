#pragma once

#include "render/Shader.hpp"
#include "world/GltfModel.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace world {

// =====================================================================
// SkinnedMesh — owns GL resources + animation runtime for a glTF model.
// =====================================================================
//
// Phase 5a/b/c rolled together:
//   - 5a: uploads one VAO per primitive (positions + normals + joint
//         indices + joint weights + EBO) and renders them in T-pose by
//         passing identity joint matrices to the shader.
//   - 5b: skinned vertex shader does matrix-palette skinning at the
//         vertex stage using a uniform mat4[kMaxJoints] array.
//   - 5c: updateAnimation() walks the active clip's per-joint TRS
//         keyframes at the current time, composes local matrices,
//         flattens the hierarchy to model-space, then multiplies by each
//         joint's inverse bind matrix to produce the matrix the shader
//         consumes.
//
// Joint matrix palette is sized at 80 to cover our 65-joint Armature
// with headroom. The skinned.vert declares the same size.

constexpr int kMaxJoints = 80;

class SkinnedMesh {
public:
  SkinnedMesh() = default;
  ~SkinnedMesh();

  SkinnedMesh(const SkinnedMesh&)            = delete;
  SkinnedMesh& operator=(const SkinnedMesh&) = delete;

  // Load + upload to GPU. Returns false on parse/upload error.
  bool load(const std::filesystem::path& glbPath);

  // Select an animation clip by name. If not found, falls back to the
  // first animation in the model. Resets playback time to 0.
  void setClip(const std::string& clipName);

  // Advance the active clip's playback time by dt seconds (looping).
  void update(float dtSeconds);

  // Compute joint matrices for the current pose and render. `model` is the
  // entity's world transform (position + rotation). The shader's uniforms
  // (u_viewProj, u_lightDir, u_color, u_paletteLevels, u_paletteEnabled)
  // are set by the caller; we only set u_model + u_jointMatrices here.
  void render(render::Shader& shader, const glm::mat4& modelMatrix);

  // Render with an externally-managed clip state — does NOT modify the
  // internal activeClip_/clipTime_. Used for remote players that each need
  // independent animation state without owning a separate SkinnedMesh.
  void renderAs(render::Shader& shader, const glm::mat4& modelMatrix,
                int clipIndex, float clipTime);

  // Lookup a clip index by name. Returns -1 if not found.
  int findClipIndex(const std::string& clipName) const;

  bool isLoaded()              const { return !primitives_.empty(); }
  const std::string& clipName() const { return activeClip_; }
  int  jointCount()            const { return static_cast<int>(model_.joints.size()); }
  int  animationCount()        const { return static_cast<int>(model_.animations.size()); }
  // Returns nullptr if outside the range.
  const std::string* animationNameAt(int idx) const;

private:
  struct PrimitiveGl {
    GLuint  vao = 0;
    GLuint  vboPos = 0, vboNrm = 0, vboJoint = 0, vboWeight = 0, ebo = 0;
    GLsizei indexCount = 0;
    int     materialIndex = -1;
  };

  void destroy();
  void uploadPrimitive(const GltfPrimitive& src, PrimitiveGl& dst);
  // Evaluate the current animation into `jointMatrices_`.
  void evaluatePose();

  GltfModel                model_;
  std::vector<PrimitiveGl> primitives_;
  std::vector<glm::mat4>   jointMatrices_;     // sent to shader (model-space * inverseBind)
  std::vector<glm::mat4>   localTransforms_;   // scratch buffer for evaluation
  std::vector<glm::mat4>   modelSpace_;        // scratch buffer for evaluation
  std::string              activeClip_;
  int                      activeClipIndex_ = -1;
  float                    clipTime_        = 0.0f;
};

}  // namespace world
