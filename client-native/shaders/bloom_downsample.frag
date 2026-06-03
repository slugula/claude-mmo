#version 460 core
// 13-tap downsample (Jimenez 2014 / CoD). On the first level it also applies a
// soft-knee bright-pass so only the bright parts of the scene bloom.
in  vec2 vUV;
out vec3 frag;

uniform sampler2D uSrc;
uniform vec2  uTexel;       // 1 / source size
uniform int   uBrightPass;  // 1 on the first (full-res) downsample
uniform float uThreshold;
uniform float uKnee;

vec3 prefilter(vec3 c) {
    float br   = max(c.r, max(c.g, c.b));
    float soft = clamp(br - uThreshold + uKnee, 0.0, 2.0 * uKnee);
    soft       = soft * soft / (4.0 * uKnee + 1e-4);
    float w    = max(soft, br - uThreshold) / max(br, 1e-4);
    return c * w;
}

void main() {
    vec2 t = uTexel;
    vec3 a = texture(uSrc, vUV + t * vec2(-2, -2)).rgb;
    vec3 b = texture(uSrc, vUV + t * vec2( 0, -2)).rgb;
    vec3 c = texture(uSrc, vUV + t * vec2( 2, -2)).rgb;
    vec3 d = texture(uSrc, vUV + t * vec2(-2,  0)).rgb;
    vec3 e = texture(uSrc, vUV + t * vec2( 0,  0)).rgb;
    vec3 f = texture(uSrc, vUV + t * vec2( 2,  0)).rgb;
    vec3 g = texture(uSrc, vUV + t * vec2(-2,  2)).rgb;
    vec3 h = texture(uSrc, vUV + t * vec2( 0,  2)).rgb;
    vec3 i = texture(uSrc, vUV + t * vec2( 2,  2)).rgb;
    vec3 j = texture(uSrc, vUV + t * vec2(-1, -1)).rgb;
    vec3 k = texture(uSrc, vUV + t * vec2( 1, -1)).rgb;
    vec3 l = texture(uSrc, vUV + t * vec2(-1,  1)).rgb;
    vec3 m = texture(uSrc, vUV + t * vec2( 1,  1)).rgb;

    vec3 col = e * 0.125;
    col += (a + c + g + i) * 0.03125;
    col += (b + d + f + h) * 0.0625;
    col += (j + k + l + m) * 0.125;

    if (uBrightPass == 1) col = prefilter(col);
    frag = max(col, vec3(0.0));
}
