#version 460 core
// Depth-only vertex shader for instanced obstacles. Shares the obstacle VAO
// layout (per-vertex pos+normal, per-instance pos+rotY) so the same VAO can
// be drawn through this program. Normal is bound but unused.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;        // unused
layout(location = 2) in vec3  a_instancePos;
layout(location = 3) in float a_instanceRotY;
layout(location = 5) in vec3  a_instanceUp;     // surface normal to tilt onto (default +Y)

uniform mat4 u_lightViewProj;

mat3 rotY(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, 0.0, -s,  0.0, 1.0, 0.0,  s, 0.0, c);
}

mat3 alignUpTo(vec3 n) {
    if (dot(n, n) < 0.25) return mat3(1.0);  // unset/degenerate attr → no tilt
    n = normalize(n);
    vec3  up = vec3(0.0, 1.0, 0.0);
    vec3  v  = cross(up, n);
    float c  = dot(up, n);
    if (c < -0.9999) return mat3(1.0, 0.0, 0.0,  0.0, -1.0, 0.0,  0.0, 0.0, -1.0);
    mat3 vx = mat3(0.0,  v.z, -v.y,
                  -v.z,  0.0,  v.x,
                   v.y, -v.x,  0.0);
    return mat3(1.0) + vx + (vx * vx) * (1.0 / (1.0 + c));
}

void main() {
    vec3 worldPos = alignUpTo(a_instanceUp) * rotY(a_instanceRotY) * a_position + a_instancePos;
    gl_Position   = u_lightViewProj * vec4(worldPos, 1.0);
}
