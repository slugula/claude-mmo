#version 460 core
// Outline pass — inflates the mesh along normals to create a border effect.
// Rendered with front-face culling so only the "back shell" is visible,
// giving a clean outline around the original geometry.

// Per-vertex (mesh)
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;

// Per-instance (one per obstacle)
layout(location = 2) in vec3  a_instancePos;
layout(location = 3) in float a_instanceRotY;

uniform mat4  u_viewProj;
uniform float u_outlineWidth;  // world-space inflation amount

mat3 rotY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat3( c, 0.0,  -s,
                0.0, 1.0, 0.0,
                  s, 0.0,   c);
}

void main() {
    mat3 R         = rotY(a_instanceRotY);
    vec3 localPos  = a_position + a_normal * u_outlineWidth;
    vec3 worldPos  = R * localPos + a_instancePos;
    gl_Position    = u_viewProj * vec4(worldPos, 1.0);
}
