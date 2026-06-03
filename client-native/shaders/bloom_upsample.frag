#version 460 core
// 3x3 tent-filter upsample. Output is additively blended (GL_ONE, GL_ONE) onto
// the next-larger mip to progressively build a wide, soft bloom.
in  vec2 vUV;
out vec3 frag;

uniform sampler2D uSrc;
uniform vec2  uTexel;   // 1 / source size
uniform float uRadius;  // filter spread

void main() {
    vec2 t = uTexel * uRadius;
    vec3 col = texture(uSrc, vUV + t * vec2(-1, -1)).rgb * 1.0;
    col += texture(uSrc, vUV + t * vec2( 0, -1)).rgb * 2.0;
    col += texture(uSrc, vUV + t * vec2( 1, -1)).rgb * 1.0;
    col += texture(uSrc, vUV + t * vec2(-1,  0)).rgb * 2.0;
    col += texture(uSrc, vUV + t * vec2( 0,  0)).rgb * 4.0;
    col += texture(uSrc, vUV + t * vec2( 1,  0)).rgb * 2.0;
    col += texture(uSrc, vUV + t * vec2(-1,  1)).rgb * 1.0;
    col += texture(uSrc, vUV + t * vec2( 0,  1)).rgb * 2.0;
    col += texture(uSrc, vUV + t * vec2( 1,  1)).rgb * 1.0;
    frag = col * (1.0 / 16.0);
}
