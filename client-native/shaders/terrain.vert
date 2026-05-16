#version 460 core
// OSRS-style terrain with optional Phase 6 directional lighting + shadow map.
//
// The vertex color carries the neighbor-averaged ground color (Gouraud
// interpolated by the rasterizer). The normal — computed in TerrainBuilder
// via central-difference on the height field — is passed through so the
// fragment shader can fold a Lambert term in at uniform-controlled strength.
// World position (== a_position for terrain — no model matrix) is also
// passed through so we can sample the shadow map in light space.

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec3 a_normal;

uniform mat4 u_viewProj;
uniform mat4 u_lightViewProj;

out vec4 v_color;
out vec3 v_normal;
out vec4 v_shadowPos;   // position in light-space clip coords

void main() {
    v_color     = a_color;
    v_normal    = a_normal;
    v_shadowPos = u_lightViewProj * vec4(a_position, 1.0);
    gl_Position = u_viewProj * vec4(a_position, 1.0);
}
