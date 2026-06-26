#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace world {

// =====================================================================
// In-memory representation of a parsed glTF model.
// =====================================================================
//
// The loader (`GltfLoader::loadGlb`) reads a .glb file via cgltf and packs
// the relevant data into these structures, throwing away cgltf's allocation
// (so the runtime is independent of cgltf after loading). All buffers are
// pre-converted into the layout the GPU pipeline wants:
//   - positions / normals as tightly-packed float[3]
//   - joint indices as uint8[4] (fits any glTF skin <= 256 joints, ours has 65)
//   - joint weights as float[4]
//   - indices as uint32 (we always re-emit; cheaper than juggling u16/u32)
//
// Animation tracks are stored per-joint per-channel; missing channels fall
// back to the joint's rest-pose TRS at evaluation time.

struct GltfPrimitive {
  std::vector<float>    positions;     // 3 floats per vertex
  std::vector<float>    normals;       // 3 floats per vertex
  std::vector<float>    colors;        // 4 floats (RGBA) per vertex from COLOR_0; empty if none
  std::vector<float>    uvs;           // 2 floats per vertex from TEXCOORD_0; empty if none
  std::vector<uint8_t>  jointIndices;  // 4 u8 per vertex (zeros if mesh isn't skinned)
  std::vector<float>    jointWeights;  // 4 floats per vertex (zeros + one 1.0 if mesh isn't skinned)
  std::vector<uint32_t> indices;
  int                   materialIndex = -1;
};

struct GltfMaterial {
  glm::vec4 baseColor = glm::vec4(1.0f);
  // Decoded baseColorTexture pixels (RGBA8, top-down). Empty = no texture.
  // Loaded from the glTF's embedded image (.glb buffer view) or an external uri.
  std::vector<uint8_t> texRGBA;
  int                  texW = 0;
  int                  texH = 0;
};

// One joint of the skin (i.e. one bone). `parent` is the index of the
// parent joint in this same array, or -1 for a root joint.
struct GltfJoint {
  std::string parentName;       // for debugging only
  int         parent = -1;
  glm::vec3   restTranslation = glm::vec3(0.0f);
  glm::quat   restRotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // identity
  glm::vec3   restScale       = glm::vec3(1.0f);
  glm::mat4   inverseBind     = glm::mat4(1.0f);
  std::string name;
};

// Per-channel keyframes for one joint within one animation. The three
// vectors are sampled independently — interpolation uses linear (or
// normalized-lerp for quats) between adjacent keyframes; out-of-range
// times clamp to the first/last sample.
struct GltfJointTrack {
  std::vector<float>     timesT;
  std::vector<glm::vec3> valuesT;
  std::vector<float>     timesR;
  std::vector<glm::quat> valuesR;   // w-first via glm::quat(w, x, y, z)
  std::vector<float>     timesS;
  std::vector<glm::vec3> valuesS;
  bool hasT = false;
  bool hasR = false;
  bool hasS = false;
};

struct GltfAnimation {
  std::string                 name;
  float                       duration = 0.0f;  // seconds
  std::vector<GltfJointTrack> tracks;           // size = number of joints
};

struct GltfModel {
  std::vector<GltfPrimitive> primitives;
  std::vector<GltfMaterial>  materials;
  std::vector<GltfJoint>     joints;        // size = joint count
  std::vector<GltfAnimation> animations;
};

}  // namespace world
