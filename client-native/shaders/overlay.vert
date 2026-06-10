#version 460 core
// OSRS-style overlay surfaces (paths, floors, shaped ground materials).
//
// Geometry is built CPU-side by OverlayRenderer: each overlay tile contributes
// triangles for its shape, draped onto the terrain height field. UVs are in
// world XZ space so textures tile continuously across adjacent tiles. The
// material (texture-array layer) is carried per-vertex as a float.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in float a_materialId;

uniform mat4 u_viewProj;
uniform mat4 u_lightViewProj;

out vec2  v_uv;
out float v_materialId;
out vec4  v_shadowPos;
out float vLinearDepth;

void main() {
    v_uv         = a_uv;
    v_materialId = a_materialId;
    v_shadowPos  = u_lightViewProj * vec4(a_position, 1.0);
    gl_Position  = u_viewProj * vec4(a_position, 1.0);
    vLinearDepth = gl_Position.w;
}
