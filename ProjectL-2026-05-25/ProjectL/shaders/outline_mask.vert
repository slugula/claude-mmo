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

uniform mat4 u_viewProj;

mat3 rotY(float angle) {
    float c = cos(angle), s = sin(angle);
    return mat3( c,   0.0, -s,
                 0.0, 1.0,  0.0,
                 s,   0.0,  c);
}

void main() {
    mat3 R      = rotY(a_instanceRotY);
    vec3 world  = R * a_position + a_instancePos;
    gl_Position = u_viewProj * vec4(world, 1.0);
}
