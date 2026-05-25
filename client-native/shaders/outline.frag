#version 460 core
// Outline pass — solid color output (typically cyan/yellow for hover glow).

uniform vec4 u_outlineColor;

out vec4 fragColor;

void main() {
    fragColor = u_outlineColor;
}
