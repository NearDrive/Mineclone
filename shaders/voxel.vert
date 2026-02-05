#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;
out float vViewDistance;

uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vec4 worldPos = vec4(aPos, 1.0);
    vec4 viewPos = uView * worldPos;

    vNormal = aNormal;
    vUV = aUV;
    vWorldPos = aPos;
    vViewDistance = length(viewPos.xyz);
    gl_Position = uProjection * viewPos;
}
