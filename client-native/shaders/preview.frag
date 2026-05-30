#version 460 core
// Simple Lambert-shaded fragment shader for the DB editor model preview.

in vec3 v_worldNormal;
in vec4 v_color;          // per-vertex RGBA (white when none)

uniform vec4 u_color;

out vec4 fragColor;

const vec3 kLightDir = normalize(vec3(1.0, 2.0, 1.0));
const float kAmbient = 0.30;
const float kDiffuse = 0.70;

void main() {
    vec3  n   = normalize(v_worldNormal);
    float d   = max(dot(n, kLightDir), 0.0);
    // glTF convention: vertex colour modulates the material/base colour.
    vec3  base = u_color.rgb * v_color.rgb;
    vec3  rgb  = base * (kAmbient + kDiffuse * d);
    fragColor  = vec4(rgb, u_color.a);
}
