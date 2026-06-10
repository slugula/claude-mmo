#version 460 core

in vec2  vUV;
in float vShoreWeight;
in vec3  vWorldPos;
in vec4  vClipPos;

uniform sampler2D uNormalMap;   // unit 0
uniform sampler2D uSceneColor;  // unit 1 — resolved FBO before water pass
uniform sampler2D uSceneDepth;  // unit 2 — resolved depth before water pass
uniform sampler2D uCausticMap;  // unit 3 — caustic texture (or procedural fallback)

// Animation
uniform float uTime;
uniform float uWaveSpeed;
uniform float uWaveScale;

// Appearance
uniform float uNormalStrength;
uniform float uReflectStrength;   // "water clarity" — how visible underwater content is (0–1)
uniform float uCausticIntensity;
uniform float uCausticScale;
uniform float uCausticSpeed;
uniform float uUseCausticMap;     // 1.0 = sample uCausticMap, 0.0 = procedural
uniform float uFoamWidth;         // 0–1: shore-zone foam band width
uniform float uFoamSpeed;
uniform float uFoamScale;
uniform float uParallaxDepth;
uniform vec3  uShallowColor;
uniform vec3  uDeepColor;
uniform vec3  uFoamColor;

// Depth-based effects
uniform float uRefractionStrength;  // UV distortion magnitude for underwater view (default 0.04)
uniform float uDepthFade;           // how fast colour transitions shallow→deep (default 5.0)
uniform float uFoamContactWidth;    // world-space depth for contact/intersection foam (default 0.3)
uniform float uShoreDepth;          // 0–1: strength of shore-distance "fake depth" for flush water
uniform float uNear;                // camera near plane
uniform float uFar;                 // camera far plane

// Per-frame lighting
uniform vec3  uCameraPos;
uniform vec3  uSunDir;
uniform float uSpecularStrength;

out vec4 fragColor;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Convert raw depth-buffer value [0,1] to linear view-space depth (world units).
float linearDepth(float rawDepth) {
    float z = rawDepth * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

// Caustics: texture-based or procedural fallback.
float causticPattern(vec2 uv, float t) {
    vec2 cuv1 = uv * uCausticScale + t * uCausticSpeed * vec2( 0.8,  0.6);
    vec2 cuv2 = uv * uCausticScale * 1.3 - t * uCausticSpeed * vec2(-0.5, 0.9) * 0.7;
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

// Animated hash noise for foam.
float foamNoise(vec2 uv, float t) {
    vec2 suv = uv * uFoamScale + vec2(t * uFoamSpeed, t * uFoamSpeed * 0.7);
    return fract(sin(dot(floor(suv), vec2(127.1, 311.7))) * 43758.5453);
}

// ---------------------------------------------------------------------------
void main() {
    // ---- Dual normal-map scroll (two directions + speeds) ------------------
    vec2 uv1 = vUV * uWaveScale + vec2( uTime * uWaveSpeed,       uTime * uWaveSpeed * 0.7);
    vec2 uv2 = vUV * uWaveScale * 0.6
             - vec2( uTime * uWaveSpeed * 0.5, uTime * uWaveSpeed);

    vec3 n1 = texture(uNormalMap, uv1).rgb * 2.0 - 1.0;
    vec3 n2 = texture(uNormalMap, uv2).rgb * 2.0 - 1.0;
    vec3 N  = normalize((n1 + n2) * vec3(uNormalStrength, 1.0, uNormalStrength));

    // ---- Screen-space coordinates ------------------------------------------
    vec2  screenUV  = (vClipPos.xy / vClipPos.w) * 0.5 + 0.5;
    float rawWaterD = vClipPos.z / vClipPos.w * 0.5 + 0.5;
    float linWaterD = linearDepth(rawWaterD);

    // ---- Scene depth at this pixel (in both raw and linear space) ----------
    float rawSceneD = texture(uSceneDepth, screenUV).r;
    float linSceneD = linearDepth(rawSceneD);

    // Water-column depth: how far below the water surface the scene geometry is.
    // Positive = scene is behind/below water; 0 = water surface touches geometry.
    float columnDepth = max(0.0, linSceneD - linWaterD);

    // ---- Depth-based water colour (shallow → deep) -------------------------
    // Two depth sources, combined by max():
    //   1. GEOMETRIC — real submerged geometry (carved beds, objects) via the
    //      water column. Near-zero for flush-draped water.
    //   2. SHORE-DISTANCE "fake depth" — open water (shoreWeight→0) reads deep,
    //      shoreline (shoreWeight→1) reads shallow. This is what gives flush
    //      water a believable depth gradient without any geometric column.
    float geoDepth   = 1.0 - exp(-columnDepth * uDepthFade);
    float openness   = 1.0 - vShoreWeight;          // 0 at shore, 1 in open water
    float shoreDepth = openness * openness * uShoreDepth;  // ease-in so edges stay shallow
    float depthFactor = max(geoDepth, shoreDepth);
    vec3  waterColor  = mix(uShallowColor, uDeepColor, depthFactor);

    // ---- Refraction: distorted sample of underwater scene ------------------
    // Displaces the scene sample using the wave normal, but only where the
    // distorted sample point is also behind the water surface (prevents
    // sampling sky/terrain that's in front of the water).
    vec2  refrUV    = clamp(screenUV + N.xz * uRefractionStrength, 0.001, 0.999);
    float refrRawD  = texture(uSceneDepth, refrUV).r;
    // refrMask = 1 when the distorted sample is behind the water surface.
    float refrMask  = step(rawWaterD + 0.001, refrRawD);
    vec2  safeUV    = mix(screenUV, refrUV, refrMask);
    vec3  sceneColor= texture(uSceneColor, safeUV).rgb;

    // Blend refracted scene into water colour. uReflectStrength = "water clarity".
    // Clearer near intersections (less depth = more transparent-looking).
    float refrBlend = uReflectStrength * clamp(1.0 - depthFactor * 0.6, 0.25, 1.0);
    waterColor = mix(waterColor, sceneColor, refrBlend * 0.55);

    // ---- Caustics (open water only) ----------------------------------------
    vec2 parallaxUV = vUV + N.xz * uParallaxDepth * (1.0 - vShoreWeight);
    float caus = causticPattern(parallaxUV, uTime) * (1.0 - vShoreWeight);
    waterColor += vec3(caus);

    // ---- Wave-crest sparkle ------------------------------------------------
    float upFacing = max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0);
    float sparkle  = pow(upFacing, 20.0) * uSpecularStrength
                   * (1.0 - vShoreWeight * 0.7);
    waterColor += vec3(sparkle * 0.80, sparkle * 0.90, sparkle * 1.00);

    // ---- Shoreline foam (driven by distance-to-land) -----------------------
    // For flush water the geometric column is ~0 everywhere, so contact foam
    // can't tell shore from open water. shore_weight knows exactly where land
    // is, so foam is banded along the shoreline. uFoamWidth sets band width.
    float foamBand = smoothstep(1.0 - clamp(uFoamWidth, 0.05, 1.0), 1.0, vShoreWeight);
    // Also fire foam where genuine geometry breaks the surface (objects/beds).
    float contactFactor = (1.0 - smoothstep(0.0, uFoamContactWidth, columnDepth))
                          * step(0.001, columnDepth);
    float foamFactor = max(foamBand, contactFactor);
    vec2  foamUV1 = vUV * uFoamScale + vec2(uTime * uFoamSpeed,       uTime * uFoamSpeed * 0.7);
    vec2  foamUV2 = vUV * uFoamScale * 0.7 + vec2(uTime * uFoamSpeed * 0.6 + 3.7,
                                                    uTime * uFoamSpeed * 0.4 + 1.3);
    float fn1 = foamNoise(foamUV1 / uFoamScale, uTime);
    float fn2 = foamNoise(foamUV2 / uFoamScale, uTime * 0.8);
    float foam = foamFactor * mix(0.55, 1.0, mix(fn1, fn2, 0.5));
    waterColor = mix(waterColor, uFoamColor, clamp(foam, 0.0, 1.0));

    // Fully opaque output — all transparency handled in-shader above.
    fragColor = vec4(waterColor, 1.0);
}
