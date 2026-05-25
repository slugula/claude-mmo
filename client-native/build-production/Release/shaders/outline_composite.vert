#version 460 core
// Full-screen triangle — no vertex buffer needed.
// Vertices for gl_VertexID 0,1,2 cover the entire clip-space quad:
//   0: (-1, -1)   1: (3, -1)   2: (-1, 3)
void main() {
    float x = (gl_VertexID == 1) ? 3.0 : -1.0;
    float y = (gl_VertexID == 2) ? 3.0 : -1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
