#version 460 core
// Obstacle fragment shader — flat base color with hemisphere-Lambert lighting
// from a single hardcoded sun direction, then run through the same HSL
// palette quantization as the terrain so obstacles sit visually in the same
// palette discipline. Phase 6 will replace this stand-in lighting with the
// real baked-vertex + shadow-map setup.

in  vec3  v_normal;
in  float vLinearDepth;
out vec4  fragColor;

uniform vec3  u_color;            // base RGB color for this obstacle type
uniform vec3  u_lightDir;         // unit vector from sun -> surface (i.e. light direction)
uniform vec3  u_paletteLevels;    // shared with terrain shader
uniform float u_paletteEnabled;   // 0 = bypass quantize, 1 = quantize
uniform float u_ambient;          // 0..1
uniform float u_diffuse;          // 0..1
uniform float u_lightingEnabled;  // 0 = flat base color, 1 = lit
uniform float u_fogEnabled;       // 0 = off, 1 = on
uniform vec3  u_fogColor;
uniform float u_fogDensity;       // e.g. 0.015
uniform float u_fogStart;         // world-units from camera before fog kicks in

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
    vec3  N      = normalize(v_normal);
    float nDotL  = max(dot(N, -normalize(u_lightDir)), 0.0);
    float lit    = clamp(u_ambient + u_diffuse * nDotL, 0.0, 1.0);
    vec3  rgb    = mix(u_color, u_color * lit, u_lightingEnabled);

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
