#version 460 core
// Minimap composite pass.
//
// Coordinate convention:
//   vUV (0,0) = FBO bottom-left -> displayed as TOP-LEFT via ImGui AddImage (Y flip).
//   center = vUV*2-1  in [-1,1].  center(0,-1) = displayed top.
//
//   X-axis convention (lookAtLH): screen-right = world -X = WEST.
//   So east tiles have center.x < 0 (left side).
//   The base texture has tileX increasing left→right (small→large UV.x).
//   We compensate by negating center.x before sampling: sample right = east. ✓
//
//   Y-axis: center.y = -1 = top = north (smaller tileY).  No flip needed.

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uBaseTex;
uniform vec2  uPlayerUV;    // player tile center in base texture UV [0,1]
uniform float uYaw;         // camera alpha_ (radians); positive = CCW drag-right
uniform vec2  uZoomUV;      // UV units per circle unit, per axis:
                            // (kPxPerTile/baseW, kPxPerTile/baseH) * tileRadius.
                            // Separate axes because the region base texture can
                            // be non-square on non-square (multi-chunk) worlds.

// Entity squares (CPU-side circle space [-1,1] — after X-flip applied in CPU)
uniform int   uDotCount;
uniform vec2  uDotPos[128];
uniform vec3  uDotColor[128];

// Destination triangle
uniform bool  uDestActive;
uniform vec2  uDestPos;     // circle space, after X-flip

// ── Helpers ───────────────────────────────────────────────────────────────────
float cross2d(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
bool inTriangle(vec2 p, vec2 a, vec2 b, vec2 c) {
    float d1 = cross2d(p - a, b - a);
    float d2 = cross2d(p - b, c - b);
    float d3 = cross2d(p - c, a - c);
    return !((d1 < 0.0 || d2 < 0.0 || d3 < 0.0) &&
             (d1 > 0.0 || d2 > 0.0 || d3 > 0.0));
}

void main() {
    vec2 center = vUV * 2.0 - 1.0;
    float dist = length(center);
    if (dist > 1.0) discard;

    // ---- Sample base texture -------------------------------------------------
    // Negate center.x to correct screen-east/west convention (lookAtLH).
    float cosA = cos(-uYaw);
    float sinA = sin(-uYaw);
    vec2 flipped = vec2(-center.x, center.y);
    vec2 rotated = vec2(
        flipped.x * cosA - flipped.y * sinA,
        flipped.x * sinA + flipped.y * cosA
    );
    vec2 sampleUV = uPlayerUV + rotated * uZoomUV;
    vec3 col = (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
                sampleUV.y < 0.0 || sampleUV.y > 1.0)
               ? vec3(0.0)
               : texture(uBaseTex, sampleUV).rgb;

    // ---- Entity squares (4×4 px, half = 2/78 ≈ 0.026 circle units) ----------
    const float kHalf      = 0.026;   // regular entities  ~4×4 px
    const float kLocalHalf = 0.040;   // local player       ~6×6 px
    for (int i = 0; i < uDotCount; i++) {
        float h = (i == 0) ? kLocalHalf : kHalf;
        vec2 d = abs(center - uDotPos[i]);
        if (d.x < h && d.y < h) col = uDotColor[i];
    }

    // ---- Destination triangle (8×8 px bounding box) -------------------------
    // Upward-pointing.  Half-height = half-width = 4/78 ≈ 0.052.
    if (uDestActive) {
        const float kTH = 0.052;  // half-height
        const float kTW = 0.052;  // half-width at base
        vec2 p  = center - uDestPos;
        vec2 tA = vec2( 0.0,  -kTH);   // tip (top)
        vec2 tB = vec2(-kTW,   kTH);   // bottom-left
        vec2 tC = vec2( kTW,   kTH);   // bottom-right
        if (inTriangle(p, tA, tB, tC)) col = vec3(0.95, 0.12, 0.12);
    }

    // ---- Gold border ring + north notch ------------------------------------
    // North direction in screen space (Y-down, X-negated): (-sin(uYaw), -cos(uYaw))
    vec2  northDir = vec2(-sin(uYaw), -cos(uYaw));
    float northDot = (dist > 0.001) ? dot(normalize(center), northDir) : 0.0;
    if (dist > 0.93) {
        col = vec3(0.55, 0.43, 0.24);          // gold border
        if (northDot > 0.97) col = vec3(1.0, 0.92, 0.25);  // north notch
    }

    float alpha = 1.0 - smoothstep(0.97, 1.0, dist);
    fragColor = vec4(col, alpha);
}
