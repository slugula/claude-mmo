#pragma once

#include "render/Shader.hpp"
#include "world/GltfModel.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

  // Select an animation clip by name. If not found, falls back to the first
  // animation in the model. Resets playback time to 0 and starts a crossfade
  // from the current visible pose into the new clip over `blendSeconds` (0 =
  // hard switch). Same clip → no-op (won't restart the loop).
  void setClip(const std::string& clipName, float blendSeconds = 0.12f);

  // Advance the active clip's playback time by dt seconds (looping).
  void update(float dtSeconds);

  // Compute joint matrices for the current pose and render. `model` is the
  // entity's world transform (position + rotation). The shader's uniforms
  // (u_viewProj, u_lightDir, u_paletteLevels, u_paletteEnabled) are set by
  // the caller; we set u_model + u_jointMatrices here.
  // When useMaterialColors=true, u_color is also set per-primitive from the
  // glTF material's baseColor (overriding the caller's u_color for each draw).
  void render(render::Shader& shader, const glm::mat4& modelMatrix,
              bool useMaterialColors = false);

  // Render with an externally-managed clip state — does NOT modify the
  // internal activeClip_/clipTime_. Used for remote players that each need
  // independent animation state without owning a separate SkinnedMesh.
  void renderAs(render::Shader& shader, const glm::mat4& modelMatrix,
                int clipIndex, float clipTime);

  // Like renderAs but crossfades between two clip states (externally managed,
  // per-remote). weight 0 = fully 'from', 1 = fully 'to'. Lets remote players
  // blend clip transitions without owning a SkinnedMesh or internal blend state.
  void renderAsBlended(render::Shader& shader, const glm::mat4& modelMatrix,
                       int fromIdx, float fromTime,
                       int toIdx,   float toTime, float weight);

  // Lookup a clip index by name. Returns -1 if not found.
  int findClipIndex(const std::string& clipName) const;

  // Prints per-joint track info for the active clip to stdout (diagnostic).
  void dumpTrackInfo() const;

  // Prints every joint index + name to stdout (used to discover the attach
  // joint when seeding SkeletonConfig for a new model).
  void dumpJointNames() const;

  // ---- Bone-socket attachment ------------------------------------------------
  // Find a joint by name; returns -1 if the loaded model has no such joint.
  int findJointIndex(const std::string& name) const;
  // Model-space transform of a joint for the MOST RECENTLY rendered pose
  // (valid only immediately after render()/renderAs(); modelSpace_ is shared
  // scratch). Returns identity if jointIndex is out of range.
  glm::mat4 jointModelMatrix(int jointIndex) const;

  bool isLoaded()              const { return !primitives_.empty(); }
  const std::string& clipName() const { return activeClip_; }
  int  jointCount()            const { return static_cast<int>(model_.joints.size()); }
  int  animationCount()        const { return static_cast<int>(model_.animations.size()); }
  // Returns nullptr if outside the range.
  const std::string* animationNameAt(int idx) const;
  // Duration of clip at `idx` in seconds. Returns `fallback` if idx is invalid.
  float clipDuration(int idx, float fallback = 0.6f) const;

private:
  struct PrimitiveGl {
    GLuint    vao = 0;
    GLuint    vboPos = 0, vboNrm = 0, vboCol = 0, vboUv = 0, vboJoint = 0, vboWeight = 0, ebo = 0;
    GLuint    texture = 0;   // baseColorTexture; 0 = none (use vertex/material colour)
    GLsizei   indexCount = 0;
    int       materialIndex = -1;
    glm::vec3 matColor = glm::vec3(1.0f);  // cached from GltfMaterial.baseColor
  };

  void destroy();
  void uploadPrimitive(const GltfPrimitive& src, PrimitiveGl& dst);
  // Sample one joint's local TRS from a clip at a time (rest pose if no track).
  void sampleJointLocal(int clipIndex, float time, int j,
                        glm::vec3& t, glm::quat& r, glm::vec3& s) const;
  // Evaluate the current animation into `jointMatrices_`. When applyBlend is
  // true (the internal-state render() path), an in-progress crossfade is mixed
  // in and the resulting per-joint TRS is cached as the "visible" pose (used as
  // the snapshot for the next transition). renderAs() passes false so remote
  // players, which share this mesh, never inherit the local player's blend.
  void evaluatePose(bool applyBlend);

  GltfModel                model_;
  std::vector<PrimitiveGl> primitives_;
  std::vector<glm::mat4>   jointMatrices_;     // sent to shader (model-space * inverseBind)
  std::vector<glm::mat4>   localTransforms_;   // scratch buffer for evaluation
  std::vector<glm::mat4>   modelSpace_;        // scratch buffer for evaluation
  std::string              activeClip_;
  int                      activeClipIndex_ = -1;
  float                    clipTime_        = 0.0f;

  // ---- Crossfade blending (internal-state render() path only) ----
  bool                     havePose_  = false;   // a visible pose has been evaluated
  bool                     blending_  = false;
  float                    blendTime_ = 0.0f;     // elapsed since transition
  float                    blendDur_  = 0.0f;     // transition length (s)
  std::vector<glm::vec3>   visT_, visS_, blendT_, blendS_;  // current + snapshot
  std::vector<glm::quat>   visR_, blendR_;
};

}  // namespace world
