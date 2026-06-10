#version 460 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec2  aUV;
layout(location = 2) in vec3  aNormal;
layout(location = 3) in float aShoreWeight;

uniform mat4  uViewProj;
uniform float uTime;
uniform float uWaveSpeed;
uniform float uWaveHeight;
uniform float uWaveScale;

out vec2  vUV;
out float vShoreWeight;
out vec3  vWorldPos;
out vec4  vClipPos;

void main() {
    // Gentle wave displacement — fully suppressed at shore so edges stay calm.
    // UPWARD-ONLY: the water sits flush on the terrain, so a wave trough that
    // dipped below the surface would expose the ground underneath. Remapping the
    // wave from [-1,1] to [0,1] keeps the surface at or above its flush resting
    // height — it only ever rises, never sinks into the terrain.
    float waveDamp = 1.0 - aShoreWeight;
    float wave = sin(aPos.x * uWaveScale        + uTime * uWaveSpeed) *
                 cos(aPos.z * uWaveScale * 0.7  + uTime * uWaveSpeed * 0.8);
    float waveUp = wave * 0.5 + 0.5;   // [0,1]
    vec3 displaced = aPos + vec3(0.0, waveUp * uWaveHeight * waveDamp, 0.0);

    vUV          = aUV;
    vShoreWeight = aShoreWeight;
    vWorldPos    = displaced;

    vec4 clip   = uViewProj * vec4(displaced, 1.0);
    vClipPos    = clip;
    gl_Position = clip;
}
