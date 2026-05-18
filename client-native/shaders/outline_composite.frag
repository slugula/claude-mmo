#version 460 core
// Screen-space outline composite.
//
// Reads a binary silhouette mask texture (rendered in a prior pass with
// outline_mask.vert/frag) and draws a clean, geometry-independent outline
// around it.
//
// Algorithm:
//   1. Sample the mask at the current pixel (center).
//   2. Sample at 16 evenly-spaced points on a circle of outlineRadius pixels.
//   3. border = max(ring samples) * (1 - center)
//      → 1 at pixels just outside the silhouette edge, 0 everywhere else.
//   4. Output outlineColor with alpha = outlineColor.a * border.
//
// The result is alpha-blended over the scene in the calling code, giving a
// pixel-perfect, gap-free outline regardless of mesh topology or face normals.

uniform sampler2D u_mask;           // R8 silhouette texture
uniform vec2      u_pixelSize;      // vec2(1.0/fbWidth, 1.0/fbHeight)
uniform float     u_outlineRadius;  // outline thickness in pixels
uniform vec4      u_outlineColor;   // RGBA

out vec4 fragColor;

void main() {
    vec2  uv     = gl_FragCoord.xy * u_pixelSize;
    float center = texture(u_mask, uv).r;

    // Ring sample: 16 points at outlineRadius pixels around the current pixel.
    float maxRing = 0.0;
    const int N = 16;
    for (int i = 0; i < N; i++) {
        float angle  = float(i) * (6.28318530718 / float(N));
        vec2  offset = vec2(cos(angle), sin(angle)) * u_outlineRadius * u_pixelSize;
        maxRing      = max(maxRing, texture(u_mask, uv + offset).r);
    }

    float border = maxRing * (1.0 - center);
    fragColor    = vec4(u_outlineColor.rgb, u_outlineColor.a * border);
}
