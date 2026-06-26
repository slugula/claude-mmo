#version 460 core
// Instanced obstacle vertex shader.
//
// Per-vertex attributes describe the LOCAL geometry of one mesh
// (trunk / canopy / rock). Per-instance attributes place each instance into
// the world with a position and Y-axis rotation. Both VBOs share the same
// VAO via attribute divisors (0 = per-vertex, 1 = per-instance).

// Per-vertex (mesh)
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 4) in vec4 a_color;     // per-vertex RGBA (white when model has none)
layout(location = 8) in vec2 a_uv;        // per-vertex UV (textured meshes only)

// Per-instance (one per obstacle)
layout(location = 2) in vec3  a_instancePos;
layout(location = 3) in float a_instanceRotY;
layout(location = 5) in vec3  a_instanceUp;   // surface normal to tilt onto (default +Y)
layout(location = 6) in vec3  a_instanceTint; // per-instance RGB tint (default white)
layout(location = 7) in vec4  a_cornerH;      // tile corner heights SW,SE,NW,NE (pool warp)

uniform mat4  u_viewProj;
uniform mat4  u_lightViewProj;
uniform float u_poolWarp;   // 0 = rigid placement (default); 1 = bilinear height warp

out vec3  v_normal;
out vec4  v_shadowPos;
out float vLinearDepth;
out vec4  v_color;
out vec3  v_tint;
out vec2  v_uv;

mat3 rotY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat3( c, 0.0,  -s,
                0.0, 1.0, 0.0,
                  s, 0.0,   c);
}

// Minimal (twist-free) rotation that maps +Y onto unit normal n.
// R = I + [v]x + [v]x^2 / (1+c), with v = up×n, c = up·n. (Default n=+Y → I.)
mat3 alignUpTo(vec3 n) {
    if (dot(n, n) < 0.25) return mat3(1.0);  // unset/degenerate attr → no tilt
    n = normalize(n);
    vec3  up = vec3(0.0, 1.0, 0.0);
    vec3  v  = cross(up, n);
    float c  = dot(up, n);
    if (c < -0.9999) return mat3(1.0, 0.0, 0.0,  0.0, -1.0, 0.0,  0.0, 0.0, -1.0);
    mat3 vx = mat3(0.0,  v.z, -v.y,
                  -v.z,  0.0,  v.x,
                   v.y, -v.x,  0.0);
    return mat3(1.0) + vx + (vx * vx) * (1.0 / (1.0 + c));
}

void main() {
    vec3 worldPos;
    if (u_poolWarp > 0.5) {
        // Pool warp: rotate in XZ only, then bilinearly displace every vertex's
        // height by the tile's 4 corner heights so the mesh conforms exactly to
        // the terrain (rims match neighbours → no gaps on lifted/sloped tiles).
        // Model spans a unit tile centred at the origin; (u,v) = local XZ + 0.5,
        // with SW=(0,0), SE=(1,0), NW=(0,1), NE=(1,1).
        vec3  rp = rotY(a_instanceRotY) * a_position;
        float u  = clamp(rp.x + 0.5, 0.0, 1.0);
        float v  = clamp(rp.z + 0.5, 0.0, 1.0);
        float hS = mix(a_cornerH.x, a_cornerH.y, u);   // SW→SE
        float hN = mix(a_cornerH.z, a_cornerH.w, u);   // NW→NE
        float h  = mix(hS, hN, v);                     // corner heights are deltas from instance y
        worldPos = vec3(rp.x + a_instancePos.x,
                        a_instancePos.y + rp.y + h,
                        rp.z + a_instancePos.z);
        // Light with the average tilted-plane normal (good enough for gentle warps).
        v_normal = alignUpTo(a_instanceUp) * rotY(a_instanceRotY) * a_normal;
    } else {
        mat3 R   = alignUpTo(a_instanceUp) * rotY(a_instanceRotY);
        worldPos = R * a_position + a_instancePos;
        v_normal = R * a_normal;
    }
    v_color       = a_color;
    v_tint        = a_instanceTint;
    v_uv          = a_uv;
    vec4 world4   = vec4(worldPos, 1.0);
    gl_Position   = u_viewProj * world4;
    v_shadowPos   = u_lightViewProj * world4;
    vLinearDepth  = gl_Position.w;
}
