#version 460 core
// Simple Lambert-shaded fragment shader for the DB editor model preview.

in vec3 v_worldNormal;
in vec4 v_color;          // per-vertex RGBA (white when none)
in vec2 v_uv;             // per-vertex UV (textured meshes)

uniform vec4      u_color;
uniform sampler2D u_albedo;       // baseColorTexture (unit 0)
uniform float     u_hasTexture;   // 0 = vertex/material colour, 1 = sample u_albedo

out vec4 fragColor;

const vec3 kLightDir = normalize(vec3(1.0, 2.0, 1.0));
const float kAmbient = 0.30;
const float kDiffuse = 0.70;

void main() {
    vec3  n   = normalize(v_worldNormal);
    float d   = max(dot(n, kLightDir), 0.0);
    // glTF convention: vertex colour modulates the material/base colour;
    // textured meshes sample the baseColorTexture instead.
    vec3  base = u_color.rgb * v_color.rgb;
    if (u_hasTexture > 0.5) base = texture(u_albedo, v_uv).rgb;
    vec3  rgb  = base * (kAmbient + kDiffuse * d);
    fragColor  = vec4(rgb, u_color.a);
}
