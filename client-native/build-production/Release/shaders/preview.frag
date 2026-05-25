#version 460 core
// Simple Lambert-shaded fragment shader for the DB editor model preview.

in vec3 v_worldNormal;

uniform vec4 u_color;

out vec4 fragColor;

const vec3 kLightDir = normalize(vec3(1.0, 2.0, 1.0));
const float kAmbient = 0.30;
const float kDiffuse = 0.70;

void main() {
    vec3  n   = normalize(v_worldNormal);
    float d   = max(dot(n, kLightDir), 0.0);
    vec3  rgb = u_color.rgb * (kAmbient + kDiffuse * d);
    fragColor = vec4(rgb, u_color.a);
}
