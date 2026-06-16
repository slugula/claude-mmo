// =====================================================================
// surface_lighting.glsl — shared sun + shadow model for ALL surface shaders.
// =====================================================================
//
// Included (via the engine's #include preprocessor) by terrain, obstacle,
// skinned, overlay — every lit mesh. One implementation so lighting and
// shadows stay identical across walls, objects, items, NPCs, players, ground.
// Owns the sun + shadow uniforms; including shaders must NOT redeclare them.
//
// Requires from the host shader:
//   - varyings v_normal (world normal) and v_shadowPos (light-clip position)
//   - gl_FragCoord (for per-fragment dither rotation)
//   - #include placed AFTER #version and the in/out block, BEFORE main().

// ---- Sun ----------------------------------------------------------------
uniform vec3  u_lightDir;          // sun direction (from sun toward surface)
uniform float u_ambient;           // 0..1 base brightness with no direct light
uniform float u_diffuse;           // 0..1 N·-L contribution
uniform float u_lightingEnabled;   // 0 = flat base color, 1 = lit

// ---- Shadow -------------------------------------------------------------
uniform sampler2D u_shadowMap;
uniform float     u_shadowsEnabled; // 0 = off, 1 = sampled
uniform float     u_shadowDarkness; // 0..1 how dark a fully shadowed pixel gets
uniform float     u_shadowBias;     // base depth bias (acne suppression)
uniform float     u_shadowSoftness; // max penumbra radius in texels (PCSS)

// 16-tap Poisson disk, reused for both the blocker search and the PCF filter.
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

// Lambert directional term (no shadow). Returns a multiplier in [0,1].
float directionalLight(vec3 N) {
    float nDotL = max(dot(N, -normalize(u_lightDir)), 0.0);
    return clamp(u_ambient + u_diffuse * nDotL, 0.0, 1.0);
}

// ---- PCSS soft shadows --------------------------------------------------
//
// Percentage-Closer Soft Shadows: contact-hardening penumbra. Three steps —
//   1) blocker search: average depth of occluders in a search region;
//   2) penumbra estimate: grows with (receiver - avgBlocker) so a caster far
//      above the ground throws a soft edge, one touching it stays crisp;
//   3) PCF with that penumbra radius.
// u_shadowSoftness scales the maximum penumbra (texels). Returns 1.0 = fully
// shadowed, 0.0 = fully lit. Single cascade.

float sampleShadowSoft(vec4 shadowPos, vec3 N) {
    vec3 proj = shadowPos.xyz / shadowPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    // The shadow pass renders back faces (front-face culled), which already
    // suppresses most acne — so the slope term is small and u_shadowBias can
    // stay tiny, avoiding the "peter-panning" detachment of bigger biases.
    float cosTheta = max(dot(N, -normalize(u_lightDir)), 0.0);
    float bias     = u_shadowBias + 0.0008 * (1.0 - cosTheta);
    float receiver = proj.z - bias;
    vec2  texel    = 1.0 / vec2(textureSize(u_shadowMap, 0));

    // Per-fragment random rotation turns the disk pattern into noise.
    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.28318;
    float ca = cos(angle), sa = sin(angle);
    mat2  rot = mat2(ca, -sa, sa, ca);

    float maxR = max(u_shadowSoftness, 1.0);

    // 1) Blocker search over the max penumbra region.
    float blockerSum = 0.0;
    float blockerCnt = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2  off = rot * kPoissonDisk[i] * texel * maxR;
        float d   = texture(u_shadowMap, proj.xy + off).r;
        if (d < receiver) { blockerSum += d; blockerCnt += 1.0; }
    }
    if (blockerCnt < 0.5) return 0.0;   // no occluders → fully lit

    // 2) Penumbra from average blocker distance (contact hardening).
    float avgBlocker = blockerSum / blockerCnt;
    float penumbra   = (receiver - avgBlocker) / max(avgBlocker, 1e-4);
    float filterR    = clamp(penumbra * maxR * 6.0, 1.0, maxR);

    // 3) PCF with the contact-hardened radius.
    float occluded = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 off = rot * kPoissonDisk[i] * texel * filterR;
        occluded += (receiver > texture(u_shadowMap, proj.xy + off).r) ? 1.0 : 0.0;
    }
    return occluded / 16.0;
}

// Convenience: full shadow attenuation multiplier (1 = lit, <1 = shadowed).
float shadowMultiplier(vec4 shadowPos, vec3 N) {
    float s = sampleShadowSoft(shadowPos, N);
    return 1.0 - u_shadowDarkness * s * u_shadowsEnabled;
}
