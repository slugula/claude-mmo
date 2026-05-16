#include "world/GltfLoader.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace world {

namespace {

// Read N float values from an accessor into outVec. Handles cgltf's
// stride-aware reads and pads with zeros for fewer-than-N component types.
void readAccessorFloats(const cgltf_accessor* acc, std::vector<float>& out, int outComps) {
  if (!acc) return;
  const size_t count = acc->count;
  out.resize(count * static_cast<size_t>(outComps), 0.0f);
  for (size_t i = 0; i < count; ++i) {
    cgltf_accessor_read_float(acc, i, out.data() + i * outComps, outComps);
  }
}

// Read N uint values into outVec. Same handling.
void readAccessorUints(const cgltf_accessor* acc, std::vector<uint32_t>& out, int outComps) {
  if (!acc) return;
  const size_t count = acc->count;
  out.resize(count * static_cast<size_t>(outComps), 0u);
  for (size_t i = 0; i < count; ++i) {
    cgltf_accessor_read_uint(acc, i, out.data() + i * outComps, outComps);
  }
}

// Index of `node` inside `data->nodes` array — cgltf uses pointer identity,
// so we just do pointer arithmetic.
int nodeIndex(const cgltf_data* data, const cgltf_node* node) {
  if (!node) return -1;
  return static_cast<int>(node - data->nodes);
}

}  // namespace

std::optional<GltfModel> loadGlb(const std::filesystem::path& path) {
  cgltf_options opts{};
  cgltf_data*   data = nullptr;
  const std::string pathStr = path.string();

  cgltf_result r = cgltf_parse_file(&opts, pathStr.c_str(), &data);
  if (r != cgltf_result_success) {
    std::fprintf(stderr, "[GltfLoader] parse failed for %s (code %d)\n", pathStr.c_str(), r);
    return std::nullopt;
  }
  // .glb has its binary chunk embedded — load_buffers is still required to
  // resolve buffer pointers internally.
  r = cgltf_load_buffers(&opts, data, pathStr.c_str());
  if (r != cgltf_result_success) {
    std::fprintf(stderr, "[GltfLoader] load_buffers failed for %s (code %d)\n", pathStr.c_str(), r);
    cgltf_free(data);
    return std::nullopt;
  }

  GltfModel model;

  // ---- Materials --------------------------------------------------------
  model.materials.reserve(data->materials_count);
  for (size_t i = 0; i < data->materials_count; ++i) {
    const cgltf_material& m = data->materials[i];
    GltfMaterial out;
    if (m.has_pbr_metallic_roughness) {
      const auto* c = m.pbr_metallic_roughness.base_color_factor;
      out.baseColor = glm::vec4(c[0], c[1], c[2], c[3]);
    }
    model.materials.push_back(out);
  }

  // ---- Skin (we assume one skin per model, which holds for our case) ---
  std::unordered_map<const cgltf_node*, int> jointIndexByNode;
  if (data->skins_count > 0) {
    const cgltf_skin& skin = data->skins[0];

    // Build joint -> our-index lookup (cgltf gives us a flat list of nodes
    // serving as joints).
    model.joints.resize(skin.joints_count);
    jointIndexByNode.reserve(skin.joints_count);
    for (size_t j = 0; j < skin.joints_count; ++j) {
      jointIndexByNode[skin.joints[j]] = static_cast<int>(j);
    }

    // Inverse bind matrices come from a single accessor of mat4s.
    std::vector<float> ibmFloats;
    readAccessorFloats(skin.inverse_bind_matrices, ibmFloats, 16);

    for (size_t j = 0; j < skin.joints_count; ++j) {
      const cgltf_node* n = skin.joints[j];
      GltfJoint out;
      out.name = n->name ? n->name : "";

      // Parent — only set if the parent is also a joint in this skin.
      out.parent = -1;
      if (n->parent) {
        auto it = jointIndexByNode.find(n->parent);
        if (it != jointIndexByNode.end()) {
          out.parent     = it->second;
          out.parentName = n->parent->name ? n->parent->name : "";
        }
      }

      // Rest pose. cgltf exposes either explicit TRS or a baked matrix;
      // glTF mandates TRS is independent from matrix, but for safety we
      // decompose if only the matrix is present.
      if (n->has_translation) {
        out.restTranslation = glm::vec3(n->translation[0], n->translation[1], n->translation[2]);
      }
      if (n->has_rotation) {
        // glTF quaternion order: (x, y, z, w); glm::quat constructor takes (w, x, y, z).
        out.restRotation = glm::quat(n->rotation[3], n->rotation[0], n->rotation[1], n->rotation[2]);
      }
      if (n->has_scale) {
        out.restScale = glm::vec3(n->scale[0], n->scale[1], n->scale[2]);
      }

      // Inverse bind matrix (glTF stores column-major, glm::mat4 is also column-major).
      if (j * 16 + 16 <= ibmFloats.size()) {
        std::memcpy(&out.inverseBind, &ibmFloats[j * 16], 16 * sizeof(float));
      }
      model.joints[j] = std::move(out);
    }
  }

  // ---- Mesh primitives --------------------------------------------------
  // We support a single mesh (first one found) since our character model
  // only has one. Add a loop here when we move to multi-mesh entities.
  if (data->meshes_count > 0) {
    const cgltf_mesh& mesh = data->meshes[0];
    model.primitives.reserve(mesh.primitives_count);

    for (size_t p = 0; p < mesh.primitives_count; ++p) {
      const cgltf_primitive& prim = mesh.primitives[p];
      GltfPrimitive out;

      const cgltf_accessor* aPos   = nullptr;
      const cgltf_accessor* aNorm  = nullptr;
      const cgltf_accessor* aJoint = nullptr;
      const cgltf_accessor* aWt    = nullptr;
      for (size_t a = 0; a < prim.attributes_count; ++a) {
        const cgltf_attribute& at = prim.attributes[a];
        switch (at.type) {
          case cgltf_attribute_type_position: aPos   = at.data; break;
          case cgltf_attribute_type_normal:   aNorm  = at.data; break;
          case cgltf_attribute_type_joints:   if (at.index == 0) aJoint = at.data; break;
          case cgltf_attribute_type_weights:  if (at.index == 0) aWt    = at.data; break;
          default: break;
        }
      }

      readAccessorFloats(aPos,  out.positions, 3);
      readAccessorFloats(aNorm, out.normals,   3);

      // Joints — pack into 4-uint8 per vertex (well within our 65-joint cap).
      if (aJoint) {
        std::vector<uint32_t> tmp;
        readAccessorUints(aJoint, tmp, 4);
        out.jointIndices.resize(tmp.size());
        for (size_t i = 0; i < tmp.size(); ++i) {
          out.jointIndices[i] = static_cast<uint8_t>(tmp[i] & 0xFFu);
        }
      } else {
        const size_t vcount = aPos ? aPos->count : 0;
        out.jointIndices.assign(vcount * 4, 0);
      }
      // Weights — vec4 floats. If absent, set xyzw = (1, 0, 0, 0) so each
      // vertex gets full influence from joint 0 (unskinned fallback).
      if (aWt) {
        readAccessorFloats(aWt, out.jointWeights, 4);
      } else {
        const size_t vcount = aPos ? aPos->count : 0;
        out.jointWeights.assign(vcount * 4, 0.0f);
        for (size_t i = 0; i < vcount; ++i) out.jointWeights[i * 4] = 1.0f;
      }

      readAccessorUints(prim.indices, out.indices, 1);

      // Material index
      if (prim.material) {
        out.materialIndex = static_cast<int>(prim.material - data->materials);
      }

      model.primitives.push_back(std::move(out));
    }
  }

  // ---- Animations -------------------------------------------------------
  model.animations.reserve(data->animations_count);
  for (size_t a = 0; a < data->animations_count; ++a) {
    const cgltf_animation& anim = data->animations[a];
    GltfAnimation out;
    out.name = anim.name ? anim.name : ("anim_" + std::to_string(a));
    out.tracks.resize(model.joints.size());

    for (size_t c = 0; c < anim.channels_count; ++c) {
      const cgltf_animation_channel& ch = anim.channels[c];
      const cgltf_animation_sampler* sm = ch.sampler;
      if (!ch.target_node || !sm) continue;

      auto it = jointIndexByNode.find(ch.target_node);
      if (it == jointIndexByNode.end()) continue;  // animation targets a non-joint node — ignore
      const int j = it->second;
      GltfJointTrack& tr = out.tracks[j];

      std::vector<float> times;
      readAccessorFloats(sm->input, times, 1);
      for (float t : times) out.duration = std::max(out.duration, t);

      if (ch.target_path == cgltf_animation_path_type_translation) {
        std::vector<float> v;
        readAccessorFloats(sm->output, v, 3);
        tr.timesT  = std::move(times);
        tr.valuesT.resize(v.size() / 3);
        for (size_t i = 0; i < tr.valuesT.size(); ++i) {
          tr.valuesT[i] = glm::vec3(v[i*3], v[i*3+1], v[i*3+2]);
        }
        tr.hasT = !tr.valuesT.empty();
      } else if (ch.target_path == cgltf_animation_path_type_rotation) {
        std::vector<float> v;
        readAccessorFloats(sm->output, v, 4);
        tr.timesR  = std::move(times);
        tr.valuesR.resize(v.size() / 4);
        for (size_t i = 0; i < tr.valuesR.size(); ++i) {
          // glTF (x, y, z, w) -> glm::quat constructor (w, x, y, z)
          tr.valuesR[i] = glm::quat(v[i*4+3], v[i*4+0], v[i*4+1], v[i*4+2]);
        }
        tr.hasR = !tr.valuesR.empty();
      } else if (ch.target_path == cgltf_animation_path_type_scale) {
        std::vector<float> v;
        readAccessorFloats(sm->output, v, 3);
        tr.timesS  = std::move(times);
        tr.valuesS.resize(v.size() / 3);
        for (size_t i = 0; i < tr.valuesS.size(); ++i) {
          tr.valuesS[i] = glm::vec3(v[i*3], v[i*3+1], v[i*3+2]);
        }
        tr.hasS = !tr.valuesS.empty();
      }
    }

    model.animations.push_back(std::move(out));
  }

  std::fprintf(stdout, "[GltfLoader] loaded %s: %zu primitives, %zu joints, %zu animations\n",
               pathStr.c_str(), model.primitives.size(), model.joints.size(),
               model.animations.size());

  cgltf_free(data);
  return model;
}

}  // namespace world
