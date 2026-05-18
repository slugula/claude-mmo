#version 460 core
//
// Skinned mesh vertex shader — matrix-palette skinning.
//
// Per-vertex: position, normal, 4 joint indices (uvec4 of u8), 4 joint
// weights. The four influences for each vertex blend their joint
// matrices, the result transforms the rest-pose vertex into model
// space, then u_model places the entity in the world, then u_viewProj
// projects to clip space.
//
// u_jointMatrices is sized at 80 to cover up to-80-joint skeletons.
// Our character has 65 joints (Armature in UAL1_Standard).

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in uvec4 a_jointIndices;
layout(location = 3) in vec4  a_jointWeights;

uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform mat4 u_jointMatrices[80];

out vec3  v_normal;
out float vLinearDepth;

void main() {
    mat4 skin =
        a_jointWeights.x * u_jointMatrices[a_jointIndices.x] +
        a_jointWeights.y * u_jointMatrices[a_jointIndices.y] +
        a_jointWeights.z * u_jointMatrices[a_jointIndices.z] +
        a_jointWeights.w * u_jointMatrices[a_jointIndices.w];

    vec4 modelPos = skin * vec4(a_position, 1.0);
    vec4 worldPos = u_model * modelPos;

    // Approximate normal transform — assumes uniform scale on joints + model.
    // Bone scales in this asset are 1 so this is exact; if a future asset
    // has non-uniform scales we'd switch to transpose(inverse(...)).
    v_normal = mat3(u_model) * mat3(skin) * a_normal;

    gl_Position  = u_viewProj * worldPos;
    vLinearDepth = gl_Position.w;
}
