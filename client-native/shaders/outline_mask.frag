#version 460 core
// Silhouette mask — outputs solid white wherever this entity is visible.
//
// The mask FBO has no depth attachment of its own, so we do a manual depth
// comparison: sample the pre-resolved scene depth texture and discard any
// fragment that lies behind already-rendered geometry. This correctly hides
// occluded surfaces without requiring a separate depth buffer on the mask FBO.
//
// An epsilon of 0.002 tolerates precision differences introduced by the
// MSAA → single-sample depth resolve.

uniform sampler2D u_sceneDepth;  // resolved scene depth (GL_DEPTH_COMPONENT)
uniform vec2      u_screenSize;  // (fbWidth, fbHeight)
uniform float     u_depthBias;   // configurable epsilon for MSAA depth resolve tolerance

out vec4 fragColor;

void main() {
    vec2  uv         = gl_FragCoord.xy / u_screenSize;
    float sceneDepth = texture(u_sceneDepth, uv).r;
    if (gl_FragCoord.z > sceneDepth + u_depthBias) discard;
    fragColor = vec4(1.0);
}
