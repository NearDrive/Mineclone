#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 vNormal;
out vec2 vUV;

uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vNormal = aNormal;
    vUV = aUV;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
