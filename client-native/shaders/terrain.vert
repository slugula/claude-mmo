#version 460 core
// OSRS-style terrain with optional Phase 6 directional lighting.
//
// The vertex color carries the neighbor-averaged ground color (Gouraud
// interpolated by the rasterizer). The normal — computed in TerrainBuilder
// via central-difference on the height field — is passed through so the
// fragment shader can fold a Lambert term in at uniform-controlled strength.

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec3 a_normal;

uniform mat4 u_viewProj;

out vec4 v_color;
out vec3 v_normal;

void main() {
    v_color     = a_color;
    v_normal    = a_normal;
    gl_Position = u_viewProj * vec4(a_position, 1.0);
}
