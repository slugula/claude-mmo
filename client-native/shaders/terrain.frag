#version 460 core
//
// OSRS-style HSL palette quantization (Phase 7).
//
// Vertex colors come in (linear RGB, AO baked in), are Gouraud-interpolated
// across each triangle by the rasterizer, then this fragment shader:
//   1. converts the interpolated RGB to HSL,
//   2. snaps each HSL channel to a discrete set of buckets,
//   3. converts back to RGB.
//
// The bucket counts are uniforms so you can tune the palette interactively.
// A typical OSRS-feel default is ~16 hues, 8 saturations, 16 lightnesses,
// but the slider goes much finer/coarser for experimentation.
//
// HSL quantization (rather than RGB) preserves hue identity when banding —
// a grass patch stays clearly green at every lightness step, instead of
// drifting through olive/yellow as it would in an RGB posterize.

in  vec4 v_color;
out vec4 fragColor;

uniform vec3  u_paletteLevels;   // (hue_levels, sat_levels, lum_levels)
uniform float u_paletteEnabled;  // 0 = bypass, 1 = quantize

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

    // Snap-then-restore via HSL gives banded but hue-stable colors.
    vec3 hsl       = rgb2hsl(rgb);
    vec3 snapped   = floor(hsl * u_paletteLevels) / u_paletteLevels;
    vec3 quantized = hsl2rgb(snapped);

    // u_paletteEnabled is a linear mix so the toggle has zero branches in the
    // shader and disabled state is exactly the raw interpolated color.
    fragColor = vec4(mix(rgb, quantized, u_paletteEnabled), v_color.a);
}
