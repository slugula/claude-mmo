#version 460 core
// Instanced obstacle vertex shader.
//
// Per-vertex attributes describe the LOCAL geometry of one mesh
// (trunk / canopy / rock). Per-instance attributes place each instance into
// the world with a position and Y-axis rotation. Both VBOs share the same
// VAO via attribute divisors (0 = per-vertex, 1 = per-instance).

// Per-vertex (mesh)
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 4) in vec4 a_color;     // per-vertex RGBA (white when model has none)

// Per-instance (one per obstacle)
layout(location = 2) in vec3  a_instancePos;
layout(location = 3) in float a_instanceRotY;

uniform mat4 u_viewProj;
uniform mat4 u_lightViewProj;

out vec3  v_normal;
out vec4  v_shadowPos;
out float vLinearDepth;
out vec4  v_color;

mat3 rotY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat3( c, 0.0,  -s,
                0.0, 1.0, 0.0,
                  s, 0.0,   c);
}

void main() {
    mat3 R        = rotY(a_instanceRotY);
    vec3 worldPos = R * a_position + a_instancePos;
    v_normal      = R * a_normal;
    v_color       = a_color;
    vec4 world4   = vec4(worldPos, 1.0);
    gl_Position   = u_viewProj * world4;
    v_shadowPos   = u_lightViewProj * world4;
    vLinearDepth  = gl_Position.w;
}
