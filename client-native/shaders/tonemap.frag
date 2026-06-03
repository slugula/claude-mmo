#version 460 core
// Final present pass: exposure → (+ bloom) → tonemap → gamma, HDR scene to the
// default LDR framebuffer. UI is drawn afterwards so it is never tonemapped.

in  vec2 vUV;
out vec4 fragColor;

uniform sampler2D uScene;        // HDR scene (RGBA16F)
uniform sampler2D uBloom;        // blurred bloom (RGBA16F)
uniform float     uExposure;     // linear multiplier before tonemap
uniform float     uBloomIntensity;
uniform int       uBloomEnabled; // 0/1
uniform int       uTonemap;      // 0 = none, 1 = Reinhard, 2 = ACES
uniform float     uGamma;        // output gamma (1.0 = no extra correction)

// Narkowicz 2015 ACES filmic approximation.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    if (uBloomEnabled == 1)
        hdr += texture(uBloom, vUV).rgb * uBloomIntensity;

    hdr *= uExposure;

    vec3 mapped;
    if      (uTonemap == 2) mapped = aces(hdr);
    else if (uTonemap == 1) mapped = hdr / (hdr + vec3(1.0));   // Reinhard
    else                    mapped = hdr;                        // none / passthrough

    mapped = pow(max(mapped, 0.0), vec3(1.0 / max(uGamma, 0.001)));
    fragColor = vec4(mapped, 1.0);
}
