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
uniform float uFoamWidth;       // 0–1: how far from the tile edge foam extends inward
uniform float uFoamSpeed;
uniform float uFoamScale;
uniform float uParallaxDepth;
uniform vec3  uShallowColor;
uniform vec3  uDeepColor;
uniform vec3  uFoamColor;

// Per-frame lighting/view state (set by host each frame)
uniform vec3  uCameraPos;           // world-space camera eye position
uniform vec3  uSunDir;              // direction FROM sun TOWARD scene (same convention as u_lightDir)
uniform float uSpecularStrength;    // 0–1, intensity of the sun specular highlight on water
uniform float uWaterAlpha;          // 0–1, overall water opacity (0.8–0.9 looks good)

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
        c1 = texture(uCausticMap, cuv1).r;
        c2 = texture(uCausticMap, cuv2).r;
        return pow(c1 * c2, 1.5) * uCausticIntensity;
    } else {
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
    vec3 N = normalize((n1 + n2) * vec3(uNormalStrength, 1.0, uNormalStrength));

    // ---- Screen-space depth shimmer -------------------------------------------
    vec2 ndcXY    = vClipPos.xy / vClipPos.w;
    vec2 screenUV = ndcXY * 0.5 + 0.5;

    float waterDepth01 = vClipPos.z / vClipPos.w * 0.5 + 0.5;

    // Fixed small perturbation — NOT scaled by uReflectStrength.
    // This decouples "how much scene is blended" from "how distorted the sample is",
    // preventing the blocky/pixelated look at high Reflect values.
    vec2 reflUV = screenUV + N.xz * 0.025;
    reflUV      = clamp(reflUV, vec2(0.001), vec2(0.999));

    // Suppress distortion where a foreground object is in front of water.
    float sceneDepthAtRefl = texture(uSceneDepth, reflUV).r;
    float edgeMask   = smoothstep(waterDepth01 - 0.015, waterDepth01, sceneDepthAtRefl);
    vec2  safeReflUV = mix(screenUV, reflUV, edgeMask);
    vec3  sceneColor = texture(uSceneColor, safeReflUV).rgb;

    // ---- Depth gradient + parallax ----------------------------------------
    vec2 parallaxUV = vUV + N.xz * uParallaxDepth * (1.0 - vShoreWeight);
    vec3 waterColor = mix(uDeepColor, uShallowColor, vShoreWeight);

    // ---- Caustics (open water only) ----------------------------------------
    float caus = causticPattern(parallaxUV, uTime) * (1.0 - vShoreWeight);
    waterColor += vec3(caus);

    // ---- Scene blend (depth shimmer / SSR) ---------------------------------
    // Caps at 0.40 so water intrinsic colour always dominates — uReflectStrength
    // tunes depth/distortion richness rather than turning the water transparent.
    float sceneBlend = uReflectStrength * 0.40 * (1.0 - vShoreWeight * 0.5);
    waterColor = mix(waterColor, sceneColor, sceneBlend);

    // ---- Wave-crest sparkle ------------------------------------------------
    // Wave facets that point directly upward (toward an overhead camera) catch
    // the light and appear bright.  As the normal map scrolls, these shimmer
    // and dance across the surface — the top-down equivalent of sun glints.
    // Uses the upward component of the wave normal; shoreline calmer.
    float upFacing = max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0);
    float sparkle  = pow(upFacing, 20.0) * uSpecularStrength
                   * (1.0 - vShoreWeight * 0.7);
    // Cool-tinted: peaks appear as white-blue glints
    waterColor += vec3(sparkle * 0.80, sparkle * 0.90, sparkle * 1.00);

    // ---- Shore-weight foam -------------------------------------------------
    float foamFactor = smoothstep(max(0.0, 1.0 - uFoamWidth), 1.0, vShoreWeight);
    float foam = foamFactor * foamNoise(vUV, uTime);
    waterColor = mix(waterColor, uFoamColor, foam);

    // Semi-transparent water so objects (fish, submerged geometry) show through.
    // Shore edges become slightly more opaque for a natural look.
    float alpha = mix(uWaterAlpha, min(uWaterAlpha + 0.10, 1.0), vShoreWeight * 0.5);
    fragColor = vec4(waterColor, alpha);
}
