#version 460 core
//
// Skinned mesh fragment shader. Identical lighting + HSL quantization to
// obstacle.frag so the player and the obstacles read in the same palette
// discipline as the terrain. Could be a shared include if GLSL had any.

in  vec3  v_normal;
in  vec4  v_shadowPos;
in  float vLinearDepth;
out vec4  fragColor;

uniform vec3      u_color;
uniform vec3      u_lightDir;
uniform vec3      u_paletteLevels;
uniform float     u_paletteEnabled;
uniform float     u_ambient;
uniform float     u_diffuse;
uniform float     u_lightingEnabled;
uniform sampler2D u_shadowMap;
uniform float     u_shadowsEnabled;
uniform float     u_shadowDarkness;
uniform float     u_shadowBias;
uniform float     u_fogEnabled;
uniform vec3      u_fogColor;
uniform float     u_fogDensity;
uniform float     u_fogStart;

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

// ---- Shadow — rotated Poisson disk PCF + slope-scale bias ---------------

const vec2 kPoissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

float sampleShadow(vec4 shadowPos, vec3 N) {
    vec3 proj = shadowPos.xyz / shadowPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float cosTheta = max(dot(N, -normalize(u_lightDir)), 0.0);
    float bias     = u_shadowBias + 0.003 * (1.0 - cosTheta);
    float current  = proj.z - bias;
    vec2  texel    = 1.0 / vec2(textureSize(u_shadowMap, 0));

    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.28318;
    float ca = cos(angle), sa = sin(angle);
    mat2  rot = mat2(ca, -sa, sa, ca);

    float occluded = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 offset = rot * kPoissonDisk[i] * texel * 2.0;
        occluded += (current > texture(u_shadowMap, proj.xy + offset).r) ? 1.0 : 0.0;
    }
    return occluded / 16.0;
}

void main() {
    vec3  N      = normalize(v_normal);
    float nDotL  = max(dot(N, -normalize(u_lightDir)), 0.0);
    float lit    = clamp(u_ambient + u_diffuse * nDotL, 0.0, 1.0);
    vec3  rgb    = mix(u_color, u_color * lit, u_lightingEnabled);

    float shadow    = sampleShadow(v_shadowPos, N);
    float shadowMul = 1.0 - u_shadowDarkness * shadow * u_shadowsEnabled;
    rgb *= shadowMul;

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
