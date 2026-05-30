#include "world/GltfLoader.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace world {

namespace {

// glTF vertex colours are linear; this engine treats colours as sRGB/display
// (terrain hex, material colours used directly), so convert on load to match —
// otherwise painted colours render noticeably too dark.
float linearToSrgb(float c) {
  c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
  return (c <= 0.0031308f) ? c * 12.92f
                           : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

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

// Compute the local transform for a single cgltf_node (TRS or matrix).
glm::mat4 nodeLocalTransform(const cgltf_node* node) {
  glm::mat4 m(1.0f);
  if (node->has_matrix) {
    // glTF matrix is column-major, same as glm.
    std::memcpy(glm::value_ptr(m), node->matrix, 16 * sizeof(float));
    return m;
  }
  if (node->has_translation)
    m = glm::translate(m, glm::vec3(node->translation[0],
                                     node->translation[1],
                                     node->translation[2]));
  if (node->has_rotation)
    m *= glm::mat4_cast(glm::quat(node->rotation[3],  // glm: (w,x,y,z)
                                   node->rotation[0],
                                   node->rotation[1],
                                   node->rotation[2]));
  if (node->has_scale)
    m = glm::scale(m, glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
  return m;
}

// Apply worldTransform to packed float[3] positions (in-place).
void applyTransformToPositions(std::vector<float>& pos, const glm::mat4& xf) {
  for (size_t i = 0; i + 2 < pos.size(); i += 3) {
    glm::vec4 v(pos[i], pos[i+1], pos[i+2], 1.0f);
    v = xf * v;
    pos[i] = v.x;  pos[i+1] = v.y;  pos[i+2] = v.z;
  }
}

// Apply the normal matrix (inverse-transpose of upper-left 3×3) to normals.
void applyTransformToNormals(std::vector<float>& nrm, const glm::mat4& xf) {
  const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xf)));
  for (size_t i = 0; i + 2 < nrm.size(); i += 3) {
    glm::vec3 n(nrm[i], nrm[i+1], nrm[i+2]);
    n = glm::normalize(nm * n);
    nrm[i] = n.x;  nrm[i+1] = n.y;  nrm[i+2] = n.z;
  }
}

// Parse all primitives of a single cgltf_mesh into `model.primitives`.
// For static (non-skinned) meshes, `worldTransform` is baked into positions/normals.
// For skinned meshes the transform is left alone (skinning handles it at runtime).
void parseMeshPrimitives(
    const cgltf_mesh& mesh,
    const cgltf_data* data,
    const glm::mat4&  worldTransform,
    GltfModel&        model)
{
  for (size_t p = 0; p < mesh.primitives_count; ++p) {
    const cgltf_primitive& prim = mesh.primitives[p];
    GltfPrimitive out;

    const cgltf_accessor* aPos   = nullptr;
    const cgltf_accessor* aNorm  = nullptr;
    const cgltf_accessor* aJoint = nullptr;
    const cgltf_accessor* aWt    = nullptr;
    // All COLOR_n sets, indexed by their set number (COLOR_0, COLOR_1, …).
    std::vector<const cgltf_accessor*> colorSets;
    for (size_t a = 0; a < prim.attributes_count; ++a) {
      const cgltf_attribute& at = prim.attributes[a];
      switch (at.type) {
        case cgltf_attribute_type_position: aPos   = at.data; break;
        case cgltf_attribute_type_normal:   aNorm  = at.data; break;
        case cgltf_attribute_type_joints:   if (at.index == 0) aJoint = at.data; break;
        case cgltf_attribute_type_weights:  if (at.index == 0) aWt    = at.data; break;
        case cgltf_attribute_type_color: {
          if (static_cast<int>(colorSets.size()) <= at.index)
            colorSets.resize(at.index + 1, nullptr);
          colorSets[at.index] = at.data;
          break;
        }
        default: break;
      }
    }

    readAccessorFloats(aPos,  out.positions, 3);
    readAccessorFloats(aNorm, out.normals,   3);

    // Vertex colors. glTF's primary set is COLOR_0, but Blender can export a
    // stray all-white set as COLOR_0 while the painted colours land in COLOR_1.
    // So read into RGBA, and if a set is uniformly white but a later set carries
    // real paint, use that instead. cgltf normalizes ubyte/ushort and converts
    // VEC3/VEC4 transparently.
    auto readColorSet = [](const cgltf_accessor* acc, std::vector<float>& dst, bool& allWhite) {
      const int    ncomp = static_cast<int>(cgltf_num_components(acc->type));
      const size_t count = acc->count;
      dst.assign(count * 4, 1.0f);
      allWhite = true;
      for (size_t i = 0; i < count; ++i) {
        float tmp[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        cgltf_accessor_read_float(acc, i, tmp, static_cast<cgltf_size>(ncomp));
        if (tmp[0] < 0.996f || tmp[1] < 0.996f || tmp[2] < 0.996f) allWhite = false;
        // glTF colours are linear → convert to sRGB to match the engine.
        dst[i*4+0]=linearToSrgb(tmp[0]); dst[i*4+1]=linearToSrgb(tmp[1]); dst[i*4+2]=linearToSrgb(tmp[2]);
        dst[i*4+3]=(ncomp>=4)?tmp[3]:1.0f;
      }
    };
    for (const cgltf_accessor* acc : colorSets) {
      if (!acc) continue;
      std::vector<float> cand; bool allWhite = true;
      readColorSet(acc, cand, allWhite);
      out.colors = std::move(cand);     // keep the latest read as a baseline
      if (!allWhite) break;             // found a painted set → use it
    }

    // Joints
    if (aJoint) {
      std::vector<uint32_t> tmp;
      readAccessorUints(aJoint, tmp, 4);
      out.jointIndices.resize(tmp.size());
      for (size_t i = 0; i < tmp.size(); ++i)
        out.jointIndices[i] = static_cast<uint8_t>(tmp[i] & 0xFFu);
    } else {
      const size_t vcount = aPos ? aPos->count : 0;
      out.jointIndices.assign(vcount * 4, 0);
    }
    // Weights
    if (aWt) {
      readAccessorFloats(aWt, out.jointWeights, 4);
    } else {
      const size_t vcount = aPos ? aPos->count : 0;
      out.jointWeights.assign(vcount * 4, 0.0f);
      for (size_t i = 0; i < vcount; ++i) out.jointWeights[i * 4] = 1.0f;
    }

    readAccessorUints(prim.indices, out.indices, 1);

    if (prim.material)
      out.materialIndex = static_cast<int>(prim.material - data->materials);

    // For static meshes bake the world transform into geometry so the model
    // renders correctly when placed at the origin with identity instance xf.
    const bool isSkinned = (aJoint != nullptr && aWt != nullptr);
    if (!isSkinned && worldTransform != glm::mat4(1.0f)) {
      applyTransformToPositions(out.positions, worldTransform);
      applyTransformToNormals  (out.normals,   worldTransform);
    }

    model.primitives.push_back(std::move(out));
  }
}

// Recurse through a scene node and all its children.
void processNode(
    const cgltf_data*   data,
    const cgltf_node*   node,
    const glm::mat4&    parentWorld,
    GltfModel&          model)
{
  const glm::mat4 world = parentWorld * nodeLocalTransform(node);
  if (node->mesh) parseMeshPrimitives(*node->mesh, data, world, model);
  for (size_t i = 0; i < node->children_count; ++i)
    processNode(data, node->children[i], world, model);
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

  // ---- Mesh primitives — full scene node traversal ----------------------
  // Walk the scene graph so every node's mesh is loaded, with each node's
  // accumulated world transform baked into the geometry for static meshes.
  // Skinned meshes keep geometry in bind-pose (skinning handles the xf).
  {
    const cgltf_scene* scene = data->scene;
    if (!scene && data->scenes_count > 0) scene = &data->scenes[0];

    if (scene) {
      for (size_t ni = 0; ni < scene->nodes_count; ++ni)
        processNode(data, scene->nodes[ni], glm::mat4(1.0f), model);
    } else {
      // Fallback: process every root node (no parent) if no scene is defined.
      for (size_t ni = 0; ni < data->nodes_count; ++ni)
        if (!data->nodes[ni].parent)
          processNode(data, &data->nodes[ni], glm::mat4(1.0f), model);
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
