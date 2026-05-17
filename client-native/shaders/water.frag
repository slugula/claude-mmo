#version 460 core

in vec2  vUV;
in float vShoreWeight;
in vec3  vWorldPos;
in vec4  vClipPos;

uniform sampler2D uNormalMap;   // unit 0
uniform sampler2D uSceneColor;  // unit 1 — resolved FBO before water pass

uniform float uTime;
uniform float uWaveSpeed;
uniform float uWaveScale;
uniform float uNormalStrength;
uniform float uReflectStrength;
uniform float uCausticIntensity;
uniform float uCausticScale;
uniform float uCausticSpeed;
uniform float uFoamThreshold;
uniform float uFoamSpeed;
uniform float uFoamScale;
uniform float uParallaxDepth;
uniform vec3  uShallowColor;
uniform vec3  uDeepColor;
uniform vec3  uFoamColor;

out vec4 fragColor;

// ---------------------------------------------------------------------------
// Procedural caustics: interference of two sine waves
// ---------------------------------------------------------------------------
float causticPattern(vec2 uv, float t) {
    vec2 d1 = vec2(0.8, 0.6);
    vec2 d2 = vec2(-0.5, 0.9);
    float c1 = abs(sin(dot(uv * uCausticScale + t * uCausticSpeed, d1)));
    float c2 = abs(sin(dot(uv * uCausticScale * 1.3 - t * uCausticSpeed * 0.7, d2)));
    return pow(c1 * c2, 2.0) * uCausticIntensity;
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

    // ---- Foam at shoreline edge --------------------------------------------
    float foam = step(uFoamThreshold, vShoreWeight) * foamNoise(vUV, uTime);
    waterColor = mix(waterColor, uFoamColor, foam);

    // Semi-transparent: alpha 0.85 so terrain/seabed shows through near shore
    float alpha = mix(0.92, 0.70, vShoreWeight);
    fragColor = vec4(waterColor, alpha);
}
