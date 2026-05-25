#version 460 core
// Fullscreen triangle — no VBO required, uses gl_VertexID.
// Covers the viewport with a single triangle spanning [-1, -1] to [3, 3].

out vec2 vUV;   // [0,1] with (0,0) = bottom-left of FBO

void main() {
    // Three vertices: (0,-1), (0,3), (2,1) ... using bit tricks
    vec2 pos = vec2(float((gl_VertexID & 1) << 2), float((gl_VertexID & 2) << 1)) - 1.0;
    vUV = (pos + 1.0) * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
