#version 460 core
// Depth-only vertex shader for instanced obstacles. Shares the obstacle VAO
// layout (per-vertex pos+normal, per-instance pos+rotY) so the same VAO can
// be drawn through this program. Normal is bound but unused.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;        // unused
layout(location = 2) in vec3  a_instancePos;
layout(location = 3) in float a_instanceRotY;

uniform mat4 u_lightViewProj;

mat3 rotY(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, 0.0, -s,  0.0, 1.0, 0.0,  s, 0.0, c);
}

void main() {
    vec3 worldPos = rotY(a_instanceRotY) * a_position + a_instancePos;
    gl_Position   = u_lightViewProj * vec4(worldPos, 1.0);
}
