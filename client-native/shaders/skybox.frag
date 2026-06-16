#version 460 core
// Skybox fragment shader. With a cubemap loaded (u_hasCubemap=1) it samples the
// imported sky; otherwise it synthesizes a 3-stop vertical gradient
// (ground -> horizon -> zenith) so the sky always renders with zero assets.
//
// Future (astrology): a star/constellation layer can be composited here or in a
// second pass over this base — keep this shader the "base sky" only.

in  vec3 v_dir;
out vec4 fragColor;

uniform samplerCube u_sky;
uniform float u_hasCubemap;   // 0 = procedural gradient, 1 = sample cubemap
uniform vec3  u_zenith;       // straight-up color
uniform vec3  u_horizon;      // horizon band color
uniform vec3  u_ground;       // straight-down color
uniform float u_exposure;     // overall brightness multiplier

void main() {
    vec3 d = normalize(v_dir);

    // Procedural gradient: blend by altitude (d.y in [-1,1]); sqrt easing keeps
    // a broad horizon band rather than a hard line.
    vec3 grad;
    if (d.y >= 0.0) grad = mix(u_horizon, u_zenith, pow(d.y, 0.5));
    else            grad = mix(u_horizon, u_ground, pow(-d.y, 0.5));

    vec3 col = mix(grad, texture(u_sky, d).rgb, u_hasCubemap);
    fragColor = vec4(col * u_exposure, 1.0);
}
