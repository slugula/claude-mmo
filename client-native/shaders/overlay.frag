#version 460 core
// Overlay fragment shader — textured ground surfaces with the same lighting,
// shadow, fog, and HSL-palette pipeline as terrain.frag so overlays blend in.
//
// Overlays are draped flat onto the terrain, so we light them with a fixed
// up-facing normal (a path doesn't self-shade); slope shading is left to the
// underlying terrain showing through the un-overlaid area.

in  vec2  v_uv;
in  float v_materialId;
in  vec4  v_shadowPos;
in  float vLinearDepth;
out vec4  fragColor;

uniform sampler2DArray u_texArray;

uniform vec3      u_paletteLevels;
uniform float     u_paletteEnabled;
uniform float     u_fogEnabled;
uniform vec3      u_fogColor;
uniform float     u_fogDensity;
uniform float     u_fogStart;

// Shared sun + soft-shadow model (same as terrain/obstacle/skinned).
#include "lib/surface_lighting.glsl"

// ---- HSL <-> RGB (identical to terrain.frag) ----------------------------
vec3 rgb2hsl(vec3 c) {
    float maxC = max(max(c.r, c.g), c.b);
    float minC = min(min(c.r, c.g), c.b);
    float l    = (maxC + minC) * 0.5;
    float h = 0.0, s = 0.0;
    if (maxC != minC) {
        float d = maxC - minC;
        s = l > 0.5 ? d / (2.0 - maxC - minC) : d / (maxC + minC);
        if (maxC == c.r)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (maxC == c.g) h = (c.b - c.r) / d + 2.0;
        else                  h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, s, l);
}
float hue2rgb(float p, float q, float t) {
    t = mod(t, 1.0);
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 0.5)     return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}
vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    if (s == 0.0) return vec3(l);
    float q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;
    return vec3(hue2rgb(p, q, h + 1.0/3.0), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0/3.0));
}

void main() {
    vec3 rgb = texture(u_texArray, vec3(v_uv, floor(v_materialId + 0.5))).rgb;

    // Flat up-facing normal for overlay lighting.
    vec3  N     = vec3(0.0, 1.0, 0.0);
    float lit   = directionalLight(N);
    rgb = mix(rgb, rgb * lit, u_lightingEnabled);

    rgb *= shadowMultiplier(v_shadowPos, N);

    vec3 hsl       = rgb2hsl(rgb);
    vec3 snapped   = floor(hsl * u_paletteLevels) / u_paletteLevels;
    vec3 quantized = hsl2rgb(snapped);
    fragColor = vec4(mix(rgb, quantized, u_paletteEnabled), 1.0);

    float dist      = max(0.0, vLinearDepth - u_fogStart);
    float fogFactor = clamp(exp(-u_fogDensity * dist), 0.0, 1.0);
    fogFactor       = mix(1.0, fogFactor, u_fogEnabled);
    fragColor.rgb   = mix(u_fogColor, fragColor.rgb, fogFactor);
}
