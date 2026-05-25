#version 460 core
// Wireframe overlay — uses the same VAO as the terrain mesh, ignores its
// color attribute. Fragment shader outputs solid white.
//
// A small negative bias on clip-space Z pulls the lines forward in the depth
// test so they beat the underlying filled triangles without z-fighting. This
// works for GL_LINES primitives, unlike glPolygonOffset which only affects
// polygon (filled / line-mode) rasterization.

layout(location = 0) in vec3 a_position;

uniform mat4 u_viewProj;

void main() {
    gl_Position = u_viewProj * vec4(a_position, 1.0);
    // Generous clip-space depth bias — large enough to dwarf MSAA per-sample
    // depth jitter on line vs polygon rasterization paths.
    gl_Position.z -= 0.002 * gl_Position.w;
}
