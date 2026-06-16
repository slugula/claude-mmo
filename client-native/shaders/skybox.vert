#version 460 core
// Skybox: a unit cube drawn around the camera. The cube-corner position doubles
// as the sample direction. gl_Position.z is forced to w so the sky sits exactly
// on the far plane (depth 1.0), letting the opaque scene draw over it.
layout(location = 0) in vec3 a_pos;

uniform mat4 u_viewProjNoTrans;   // projection * (view with translation removed)

out vec3 v_dir;

void main() {
    v_dir = a_pos;
    vec4 clip = u_viewProjNoTrans * vec4(a_pos, 1.0);
    gl_Position = clip.xyww;       // z = w → depth 1.0 (far plane)
}
