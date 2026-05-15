#version 460 core
// OSRS-style unlit terrain: pure per-vertex color, Gouraud-interpolated by
// the GPU across each triangle. No textures, no lighting. Phase 7 will add
// in-shader HSL palette quantization on top of this.

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;

uniform mat4 u_viewProj;

out vec4 v_color;

void main() {
    v_color     = a_color;
    gl_Position = u_viewProj * vec4(a_position, 1.0);
}
