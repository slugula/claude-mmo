#version 460 core
// Full-screen triangle — no vertex buffer needed. Emits a UV in [0,1] for
// post-processing passes (tonemap, bloom).
out vec2 vUV;
void main() {
    float x = (gl_VertexID == 1) ? 3.0 : -1.0;
    float y = (gl_VertexID == 2) ? 3.0 : -1.0;
    vUV = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
