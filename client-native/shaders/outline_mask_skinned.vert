#version 460 core
// Skinned variant of the outline mask vertex shader. Applies matrix-palette
// skinning (same math as skinned.vert) and outputs only clip position — the
// mask fragment shader (outline_mask.frag) keys off gl_FragCoord depth only, so
// no varyings are needed. Used to silhouette animated objects (e.g. fishing
// spots / node-animated props) for the screen-space outline.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal_unused;   // present in the skinned VAO
layout(location = 2) in uvec4 a_jointIndices;
layout(location = 3) in vec4  a_jointWeights;

uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform mat4 u_jointMatrices[80];

void main() {
    mat4 skin =
        a_jointWeights.x * u_jointMatrices[a_jointIndices.x] +
        a_jointWeights.y * u_jointMatrices[a_jointIndices.y] +
        a_jointWeights.z * u_jointMatrices[a_jointIndices.z] +
        a_jointWeights.w * u_jointMatrices[a_jointIndices.w];

    vec4 worldPos = u_model * (skin * vec4(a_position, 1.0));
    gl_Position   = u_viewProj * worldPos;
}
