#version 460 core
//
// OSRS-style HSL palette quantization (Phase 7) + Phase 6 directional
// lighting.
//
// Pipeline per fragment:
//   1. start from the interpolated base color (neighbor-averaged ground hue)
//   2. multiply by ambient + Lambert(N . -L) — Phase 6, gated by u_lightingEnabled
//   3. HSL-quantize the lit color so banding follows the palette discipline
//
// The bucket counts are uniforms so you can tune the palette interactively.
// HSL quantization (rather than RGB) preserves hue identity when banding —
// a grass patch stays clearly green at every lightness step, instead of
// drifting through olive/yellow as it would in an RGB posterize.

in  vec4  v_color;
in  vec3  v_normal;
in  vec4  v_shadowPos;
in  float vLinearDepth;
out vec4  fragColor;

uniform vec3      u_paletteLevels;    // (hue_levels, sat_levels, lum_levels)
uniform float     u_paletteEnabled;   // 0 = bypass quantize, 1 = quantize
uniform float     u_fogEnabled;       // 0 = off, 1 = on
uniform vec3      u_fogColor;
uniform float     u_fogDensity;       // e.g. 0.015
uniform float     u_fogStart;         // world-units from camera before fog kicks in
uniform float     u_aoEnabled;        // 0 = off, 1 = on
uniform float     u_aoStrength;       // 0..1, default 0.5

// Shared sun + soft-shadow model (u_lightDir/u_ambient/u_diffuse/u_lighting*,
// u_shadow*, directionalLight(), shadowMultiplier()).
#include "lib/surface_lighting.glsl"

// ---- HSL <-> RGB --------------------------------------------------------

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
    return vec3(
        hue2rgb(p, q, h + 1.0/3.0),
        hue2rgb(p, q, h),
        hue2rgb(p, q, h - 1.0/3.0)
    );
}

// ---- Main ---------------------------------------------------------------

void main() {
    vec3 rgb = v_color.rgb;

    // Ambient Occlusion — darkens concave areas. AO is packed into v_color.a.
    float ao    = v_color.a;
    float aoMul = 1.0 - ao * u_aoStrength * u_aoEnabled;
    rgb *= aoMul;

    // Phase 6 — Lambert directional lighting. u_lightDir points from the sun
    // toward the world, so the surface-incident vector is -u_lightDir.
    vec3  N     = normalize(v_normal);
    float lit   = directionalLight(N);
    rgb = mix(rgb, rgb * lit, u_lightingEnabled);

    // Soft (PCSS) directional shadow — shared with every other surface shader.
    rgb *= shadowMultiplier(v_shadowPos, N);

    // Snap-then-restore via HSL gives banded but hue-stable colors.
    vec3 hsl       = rgb2hsl(rgb);
    vec3 snapped   = floor(hsl * u_paletteLevels) / u_paletteLevels;
    vec3 quantized = hsl2rgb(snapped);

    fragColor = vec4(mix(rgb, quantized, u_paletteEnabled), 1.0);

    // Exponential distance fog.
    float dist      = max(0.0, vLinearDepth - u_fogStart);
    float fogFactor = clamp(exp(-u_fogDensity * dist), 0.0, 1.0);
    fogFactor       = mix(1.0, fogFactor, u_fogEnabled);
    fragColor.rgb   = mix(u_fogColor, fragColor.rgb, fogFactor);
}
