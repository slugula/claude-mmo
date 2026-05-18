#include "world/SkinnedMesh.hpp"
#include "world/GltfLoader.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace world {

namespace {

// Find the last keyframe index with time <= t. If t is before the first
// keyframe, returns 0; if past the last, returns last - 1.
size_t findKeyframe(const std::vector<float>& times, float t) {
  if (times.empty()) return 0;
  if (t <= times.front()) return 0;
  if (t >= times.back())  return times.size() - 1;
  // Linear scan — clips are short enough (typically <= 60 frames) that
  // a binary search isn't worth the complexity.
  for (size_t i = 0; i + 1 < times.size(); ++i) {
    if (t < times[i + 1]) return i;
  }
  return times.size() - 1;
}

// Sample a vec3 channel (translation or scale) at time t with linear lerp.
glm::vec3 sampleVec3(const std::vector<float>& times,
                     const std::vector<glm::vec3>& values,
                     float t, const glm::vec3& fallback) {
  if (times.empty() || values.empty()) return fallback;
  if (t <= times.front()) return values.front();
  if (t >= times.back())  return values.back();
  const size_t i  = findKeyframe(times, t);
  const size_t i1 = std::min(i + 1, times.size() - 1);
  const float  t0 = times[i];
  const float  t1 = times[i1];
  const float  a  = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
  return glm::mix(values[i], values[i1], a);
}

// Sample a quaternion channel at time t with linear interpolation +
// renormalization. For neighbouring keyframes that's indistinguishable
// from slerp at the precision we care about, and cheaper. If quaternions
// land on opposite hemispheres we flip the second one so the lerp takes
// the short path.
glm::quat sampleQuat(const std::vector<float>& times,
                     const std::vector<glm::quat>& values,
                     float t, const glm::quat& fallback) {
  if (times.empty() || values.empty()) return fallback;
  if (t <= times.front()) return glm::normalize(values.front());
  if (t >= times.back())  return glm::normalize(values.back());
  const size_t i  = findKeyframe(times, t);
  const size_t i1 = std::min(i + 1, times.size() - 1);
  const float  t0 = times[i];
  const float  t1 = times[i1];
  const float  a  = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
  glm::quat q0 = values[i];
  glm::quat q1 = values[i1];
  if (glm::dot(q0, q1) < 0.0f) q1 = -q1;
  return glm::normalize(glm::mix(q0, q1, a));
}

glm::mat4 trsToMatrix(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
  // T * R * S — the conventional glTF compose order.
  glm::mat4 m = glm::translate(glm::mat4(1.0f), t);
  m = m * glm::mat4_cast(r);
  m = glm::scale(m, s);
  return m;
}

}  // namespace

SkinnedMesh::~SkinnedMesh() {
  destroy();
}

void SkinnedMesh::destroy() {
  for (auto& p : primitives_) {
    if (p.ebo)        glDeleteBuffers(1, &p.ebo);
    if (p.vboWeight)  glDeleteBuffers(1, &p.vboWeight);
    if (p.vboJoint)   glDeleteBuffers(1, &p.vboJoint);
    if (p.vboNrm)     glDeleteBuffers(1, &p.vboNrm);
    if (p.vboPos)     glDeleteBuffers(1, &p.vboPos);
    if (p.vao)        glDeleteVertexArrays(1, &p.vao);
  }
  primitives_.clear();
}

bool SkinnedMesh::load(const std::filesystem::path& glbPath) {
  destroy();

  auto parsed = loadGlb(glbPath);
  if (!parsed) return false;
  model_ = std::move(*parsed);

  // Allocate animation evaluation scratch buffers
  const int jc = static_cast<int>(model_.joints.size());
  jointMatrices_.assign(jc, glm::mat4(1.0f));
  localTransforms_.assign(jc, glm::mat4(1.0f));
  modelSpace_.assign(jc, glm::mat4(1.0f));

  // Upload each primitive
  primitives_.resize(model_.primitives.size());
  for (size_t i = 0; i < model_.primitives.size(); ++i) {
    uploadPrimitive(model_.primitives[i], primitives_[i]);
  }

  // Default to T-pose (identity joint matrices); setClip pivots from here.
  activeClipIndex_ = -1;
  activeClip_.clear();
  clipTime_ = 0.0f;

  return true;
}

void SkinnedMesh::uploadPrimitive(const GltfPrimitive& src, PrimitiveGl& dst) {
  glCreateBuffers(1, &dst.vboPos);
  glCreateBuffers(1, &dst.vboNrm);
  glCreateBuffers(1, &dst.vboJoint);
  glCreateBuffers(1, &dst.vboWeight);
  glCreateBuffers(1, &dst.ebo);

  glNamedBufferStorage(dst.vboPos,
                       static_cast<GLsizeiptr>(src.positions.size() * sizeof(float)),
                       src.positions.data(), 0);
  glNamedBufferStorage(dst.vboNrm,
                       static_cast<GLsizeiptr>(src.normals.size() * sizeof(float)),
                       src.normals.data(), 0);
  glNamedBufferStorage(dst.vboJoint,
                       static_cast<GLsizeiptr>(src.jointIndices.size() * sizeof(uint8_t)),
                       src.jointIndices.data(), 0);
  glNamedBufferStorage(dst.vboWeight,
                       static_cast<GLsizeiptr>(src.jointWeights.size() * sizeof(float)),
                       src.jointWeights.data(), 0);
  glNamedBufferStorage(dst.ebo,
                       static_cast<GLsizeiptr>(src.indices.size() * sizeof(uint32_t)),
                       src.indices.data(), 0);

  glCreateVertexArrays(1, &dst.vao);
  // Attribute 0: position (vec3, per-vertex)
  glVertexArrayVertexBuffer(dst.vao, 0, dst.vboPos, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(dst.vao, 0);
  glVertexArrayAttribFormat(dst.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(dst.vao, 0, 0);
  // Attribute 1: normal (vec3, per-vertex)
  glVertexArrayVertexBuffer(dst.vao, 1, dst.vboNrm, 0, sizeof(float) * 3);
  glEnableVertexArrayAttrib(dst.vao, 1);
  glVertexArrayAttribFormat(dst.vao, 1, 3, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(dst.vao, 1, 1);
  // Attribute 2: joint indices (uvec4 of u8)
  glVertexArrayVertexBuffer(dst.vao, 2, dst.vboJoint, 0, sizeof(uint8_t) * 4);
  glEnableVertexArrayAttrib(dst.vao, 2);
  // AttribIFormat (integer attribute — NOT normalized, comes through as uvec4)
  glVertexArrayAttribIFormat(dst.vao, 2, 4, GL_UNSIGNED_BYTE, 0);
  glVertexArrayAttribBinding(dst.vao, 2, 2);
  // Attribute 3: joint weights (vec4, per-vertex)
  glVertexArrayVertexBuffer(dst.vao, 3, dst.vboWeight, 0, sizeof(float) * 4);
  glEnableVertexArrayAttrib(dst.vao, 3);
  glVertexArrayAttribFormat(dst.vao, 3, 4, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(dst.vao, 3, 3);

  glVertexArrayElementBuffer(dst.vao, dst.ebo);
  dst.indexCount    = static_cast<GLsizei>(src.indices.size());
  dst.materialIndex = src.materialIndex;
}

void SkinnedMesh::setClip(const std::string& clipName) {
  activeClipIndex_ = -1;
  for (size_t i = 0; i < model_.animations.size(); ++i) {
    if (model_.animations[i].name == clipName) {
      activeClipIndex_ = static_cast<int>(i);
      break;
    }
  }
  if (activeClipIndex_ < 0 && !model_.animations.empty()) {
    activeClipIndex_ = 0;  // safe fallback
  }
  activeClip_ = (activeClipIndex_ >= 0) ? model_.animations[activeClipIndex_].name : "";
  clipTime_   = 0.0f;
}

void SkinnedMesh::update(float dtSeconds) {
  if (activeClipIndex_ < 0) return;
  const auto& anim = model_.animations[activeClipIndex_];
  if (anim.duration <= 0.0f) return;
  clipTime_ += dtSeconds;
  // Loop. fmod is fine — keyframe sampling clamps for out-of-range times anyway.
  if (clipTime_ >= anim.duration) {
    clipTime_ -= anim.duration * std::floor(clipTime_ / anim.duration);
  }
}

void SkinnedMesh::evaluatePose() {
  const int jc = static_cast<int>(model_.joints.size());
  if (jc <= 0) return;

  // For each joint, start from its rest TRS. If the active clip has
  // channels for that joint, sample them.
  const GltfAnimation* anim = (activeClipIndex_ >= 0)
      ? &model_.animations[activeClipIndex_]
      : nullptr;

  for (int j = 0; j < jc; ++j) {
    const GltfJoint& joint = model_.joints[j];
    glm::vec3 t = joint.restTranslation;
    glm::quat r = joint.restRotation;
    glm::vec3 s = joint.restScale;

    if (anim && j < static_cast<int>(anim->tracks.size())) {
      const GltfJointTrack& tr = anim->tracks[j];
      if (tr.hasT) t = sampleVec3(tr.timesT, tr.valuesT, clipTime_, t);
      if (tr.hasR) r = sampleQuat(tr.timesR, tr.valuesR, clipTime_, r);
      if (tr.hasS) s = sampleVec3(tr.timesS, tr.valuesS, clipTime_, s);
    }

    localTransforms_[j] = trsToMatrix(t, r, s);
  }

  // Walk hierarchy: parents come before children in glTF skin.joints by
  // convention but we don't rely on that — we compute on demand. For our
  // 65-joint skeleton the lookup is cheap.
  for (int j = 0; j < jc; ++j) {
    const int parent = model_.joints[j].parent;
    if (parent < 0) {
      modelSpace_[j] = localTransforms_[j];
    } else {
      modelSpace_[j] = modelSpace_[parent] * localTransforms_[j];
    }
  }

  // Final palette: model-space * inverseBind. Sized to kMaxJoints in the
  // uniform; any unused tail stays whatever it was, but the shader's
  // vertex attributes will never index past `jc`.
  for (int j = 0; j < jc && j < kMaxJoints; ++j) {
    jointMatrices_[j] = modelSpace_[j] * model_.joints[j].inverseBind;
  }
}

void SkinnedMesh::render(render::Shader& shader, const glm::mat4& modelMatrix) {
  if (primitives_.empty()) return;

  evaluatePose();

  shader.setMat4("u_model", modelMatrix);

  // Upload joint matrices as a single uniform array. setMat4 only handles
  // one matrix at a time, so we hit the GL directly.
  const GLint loc = glGetUniformLocation(shader.id(), "u_jointMatrices");
  if (loc >= 0) {
    const int count = std::min(static_cast<int>(jointMatrices_.size()), kMaxJoints);
    glUniformMatrix4fv(loc, count, GL_FALSE, glm::value_ptr(jointMatrices_[0]));
  }

  for (const auto& p : primitives_) {
    glBindVertexArray(p.vao);
    glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

int SkinnedMesh::findClipIndex(const std::string& clipName) const {
  for (size_t i = 0; i < model_.animations.size(); ++i) {
    if (model_.animations[i].name == clipName) return static_cast<int>(i);
  }
  return -1;
}

void SkinnedMesh::renderAs(render::Shader& shader, const glm::mat4& modelMatrix,
                           int clipIndex, float clipTime) {
  if (primitives_.empty()) return;

  // Temporarily override internal clip state, evaluate, render, restore.
  const int    savedIndex = activeClipIndex_;
  const float  savedTime  = clipTime_;
  activeClipIndex_ = clipIndex;
  // Wrap clip time to loop the animation.
  if (clipIndex >= 0 && clipIndex < static_cast<int>(model_.animations.size())) {
    const float dur = model_.animations[clipIndex].duration;
    if (dur > 0.0f && clipTime > dur) {
      clipTime -= dur * std::floor(clipTime / dur);
    }
  }
  clipTime_ = clipTime;

  evaluatePose();

  // Restore immediately so the primary owner's state isn't corrupted.
  activeClipIndex_ = savedIndex;
  clipTime_        = savedTime;

  shader.setMat4("u_model", modelMatrix);
  const GLint loc = glGetUniformLocation(shader.id(), "u_jointMatrices");
  if (loc >= 0) {
    const int count = std::min(static_cast<int>(jointMatrices_.size()), kMaxJoints);
    glUniformMatrix4fv(loc, count, GL_FALSE, glm::value_ptr(jointMatrices_[0]));
  }
  for (const auto& p : primitives_) {
    glBindVertexArray(p.vao);
    glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
}

const std::string* SkinnedMesh::animationNameAt(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(model_.animations.size())) return nullptr;
  return &model_.animations[idx].name;
}

float SkinnedMesh::clipDuration(int idx, float fallback) const {
  if (idx < 0 || idx >= static_cast<int>(model_.animations.size())) return fallback;
  return model_.animations[idx].duration;
}

}  // namespace world
