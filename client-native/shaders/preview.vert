#version 460 core
// Minimal vertex shader for the DB editor model preview FBO.
// Takes a model matrix uniform (no instancing).

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 4) in vec4 a_color;   // per-vertex RGBA (white when none)
layout(location = 8) in vec2 a_uv;      // per-vertex UV (textured meshes)

uniform mat4 u_model;
uniform mat4 u_viewProj;

out vec3 v_worldNormal;
out vec4 v_color;
out vec2 v_uv;

void main() {
    // Normal matrix = upper-left 3x3 of model matrix (uniform scale assumed).
    v_worldNormal = mat3(u_model) * a_normal;
    v_color       = a_color;
    v_uv          = a_uv;
    gl_Position   = u_viewProj * u_model * vec4(a_position, 1.0);
}
