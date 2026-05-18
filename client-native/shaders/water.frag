#version 460 core

in vec2  vUV;
in float vShoreWeight;
in vec3  vWorldPos;
in vec4  vClipPos;

uniform sampler2D uNormalMap;   // unit 0
uniform sampler2D uSceneColor;  // unit 1 — resolved FBO before water pass
uniform sampler2D uSceneDepth;  // unit 2 — resolved depth before water pass (for foam)
uniform sampler2D uCausticMap;  // unit 3 — caustic texture (or procedural fallback)

uniform float uTime;
uniform float uWaveSpeed;
uniform float uWaveScale;
uniform float uNormalStrength;
uniform float uReflectStrength;
uniform float uCausticIntensity;
uniform float uCausticScale;
uniform float uCausticSpeed;
uniform float uUseCausticMap;   // 1.0 = sample uCausticMap, 0.0 = procedural
uniform float uFoamWidth;    // 0–1: how far from the tile edge foam extends inward
uniform float uFoamSpeed;
uniform float uFoamScale;
uniform float uParallaxDepth;
uniform vec3  uShallowColor;
uniform vec3  uDeepColor;
uniform vec3  uFoamColor;

out vec4 fragColor;

// ---------------------------------------------------------------------------
// Caustics: texture-based (when uCausticMap loaded) or procedural fallback.
// Both animate by scrolling two UV sets in different directions.
// ---------------------------------------------------------------------------
float causticPattern(vec2 uv, float t) {
    vec2 cuv1 = uv * uCausticScale + t * uCausticSpeed * vec2( 0.8,  0.6);
    vec2 cuv2 = uv * uCausticScale * 1.3 - t * uCausticSpeed * vec2(-0.5,  0.9) * 0.7;
    float c1, c2;
    if (uUseCausticMap > 0.5) {
        // Sample the user-supplied caustic image at two scrolling UVs
        c1 = texture(uCausticMap, cuv1).r;
        c2 = texture(uCausticMap, cuv2).r;
        return pow(c1 * c2, 1.5) * uCausticIntensity;
    } else {
        // Procedural: abs(sin) interference
        vec2 d1 = vec2(0.8, 0.6);
        vec2 d2 = vec2(-0.5, 0.9);
        c1 = abs(sin(dot(cuv1, d1)));
        c2 = abs(sin(dot(cuv2, d2)));
        return pow(c1 * c2, 2.0) * uCausticIntensity;
    }
}

// ---------------------------------------------------------------------------
// Procedural foam: scrolling hash noise
// ---------------------------------------------------------------------------
float foamNoise(vec2 uv, float t) {
    vec2 suv = uv * uFoamScale + vec2(t * uFoamSpeed, t * uFoamSpeed * 0.7);
    return fract(sin(dot(floor(suv), vec2(127.1, 311.7))) * 43758.5453);
}

// ---------------------------------------------------------------------------
void main() {
    // ---- Dual normal-map scroll (two different directions + speeds) --------
    vec2 uv1 = vUV * uWaveScale + vec2( uTime * uWaveSpeed,       uTime * uWaveSpeed * 0.7);
    vec2 uv2 = vUV * uWaveScale * 0.6
             - vec2( uTime * uWaveSpeed * 0.5, uTime * uWaveSpeed);

    vec3 n1 = texture(uNormalMap, uv1).rgb * 2.0 - 1.0;
    vec3 n2 = texture(uNormalMap, uv2).rgb * 2.0 - 1.0;
    // Blend normals; scale XZ by strength, keep Y dominant
    vec3 blended = normalize((n1 + n2) * vec3(uNormalStrength, 1.0, uNormalStrength));

    // ---- Screen-space reflection -------------------------------------------
    vec2 ndcXY   = vClipPos.xy / vClipPos.w;
    vec2 screenUV = ndcXY * 0.5 + 0.5;
    // Perturb sample position using XZ of the blended normal
    vec2 reflUV  = screenUV + blended.xz * 0.05 * uReflectStrength;
    reflUV       = clamp(reflUV, vec2(0.001), vec2(0.999));
    vec3 reflected = texture(uSceneColor, reflUV).rgb;

    // ---- Depth gradient + parallax ----------------------------------------
    vec2 parallaxUV = vUV + blended.xz * uParallaxDepth * (1.0 - vShoreWeight);
    vec3 waterColor = mix(uDeepColor, uShallowColor, vShoreWeight);

    // ---- Caustics (open water only) ----------------------------------------
    float caus = causticPattern(parallaxUV, uTime) * (1.0 - vShoreWeight);
    waterColor += vec3(caus);

    // ---- SSR blend (less reflection near shoreline) -------------------------
    float reflWeight = uReflectStrength * (1.0 - vShoreWeight * 0.5);
    waterColor = mix(waterColor, reflected, reflWeight);

    // ---- Shore-weight foam -------------------------------------------------
    // vShoreWeight encodes how many of the 4 adjacent tiles are non-water
    // (0 = open water, 1 = surrounded by terrain on all sides).
    // uFoamWidth [0,1] controls how far the foam band extends from the tile
    // edge inward: 0 = no foam, 0.5 = moderate band, 1 = fills all boundary
    // tiles with a smooth gradient toward the interior.
    // smoothstep maps shore_weight into a [0,1] foam factor so the band fades
    // naturally rather than cutting off with a hard edge.
    float foamFactor = smoothstep(max(0.0, 1.0 - uFoamWidth), 1.0, vShoreWeight);
    float foam = foamFactor * foamNoise(vUV, uTime);
    waterColor = mix(waterColor, uFoamColor, foam);

    // Fully opaque — terrain must not poke through the water surface.
    // The depth/shallow colour gradient + parallax already sell the depth.
    fragColor = vec4(waterColor, 1.0);
}
