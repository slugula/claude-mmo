#version 460 core
// Mask pass — renders entity/obstacle geometry at its exact world position,
// no normal inflation. Produces a silhouette texture for the screen-space
// outline composite step.

layout(location = 0) in vec3  a_position;
// location 1 (a_normal) present in the VBO but unused for the mask pass —
// the binding is kept so this shader works with the same VAO as the main render.
layout(location = 1) in vec3  a_normal_unused;

// Per-instance (one draw call = one obstacle / NPC / item)
layout(location = 2) in vec3  a_instancePos;
layout(location = 3) in float a_instanceRotY;
layout(location = 5) in vec3  a_instanceUp;   // surface normal to tilt onto (default +Y)

uniform mat4 u_viewProj;

mat3 rotY(float angle) {
    float c = cos(angle), s = sin(angle);
    return mat3( c,   0.0, -s,
                 0.0, 1.0,  0.0,
                 s,   0.0,  c);
}

// Must match obstacle.vert so the silhouette aligns with the tilted geometry.
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
    mat3 R      = alignUpTo(a_instanceUp) * rotY(a_instanceRotY);
    vec3 world  = R * a_position + a_instancePos;
    gl_Position = u_viewProj * vec4(world, 1.0);
}
