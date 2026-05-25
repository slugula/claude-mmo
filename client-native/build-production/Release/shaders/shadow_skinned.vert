#version 460 core
// Depth-only skinned mesh vertex shader for the shadow pass.
// Same attribute layout as skinned.vert so the same VAO can be drawn through
// this program without rebinding. Normal (location 1) is unused here.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;        // unused — depth pass only
layout(location = 2) in uvec4 a_jointIndices;
layout(location = 3) in vec4  a_jointWeights;

uniform mat4 u_lightViewProj;
uniform mat4 u_model;
uniform mat4 u_jointMatrices[80];

void main() {
    mat4 skin =
        a_jointWeights.x * u_jointMatrices[a_jointIndices.x] +
        a_jointWeights.y * u_jointMatrices[a_jointIndices.y] +
        a_jointWeights.z * u_jointMatrices[a_jointIndices.z] +
        a_jointWeights.w * u_jointMatrices[a_jointIndices.w];
    vec4 modelPos = skin * vec4(a_position, 1.0);
    gl_Position   = u_lightViewProj * u_model * modelPos;
}
