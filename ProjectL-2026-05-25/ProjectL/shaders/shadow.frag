#version 460 core
// Depth-only fragment shader. gl_FragDepth defaults to gl_FragCoord.z, so
// an empty main() is sufficient. We still declare a fragColor sink in case
// the FBO ever gains a color attachment for debugging.

void main() {
}
