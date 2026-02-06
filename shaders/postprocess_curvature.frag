#version 330 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;
uniform int uCurvatureEnabled;
uniform float uCurvatureStrength;
uniform float uNearPlane;
uniform float uFarPlane;

float LinearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane) / (uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane));
}

void main() {
    vec2 uv = vTexCoord;

    if (uCurvatureEnabled != 0) {
        float depth = texture(uSceneDepth, uv).r;
        float viewDistance = LinearizeDepth(depth);
        float distanceNorm = clamp((viewDistance - uNearPlane) / (uFarPlane - uNearPlane), 0.0, 1.0);
        float distanceFactor = smoothstep(0.2, 1.0, distanceNorm);
        float bottomFactor = pow(clamp(1.0 - uv.y, 0.0, 1.0), 1.5);
        float cutoff = uCurvatureStrength * distanceFactor * bottomFactor;
        uv.y += cutoff;
    }

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.08, 0.10, 0.15, 1.0);
    } else {
        FragColor = texture(uSceneColor, uv);
    }
}
