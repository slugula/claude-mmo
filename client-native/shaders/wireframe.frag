#version 460 core
// Generic colored-line shader, used for both the white wireframe grid and
// the yellow hover-tile outline. Color is uniform-controlled.

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
