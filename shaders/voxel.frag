#version 450 core

in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
in float vViewDistance;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform sampler2D uTexture;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogHeightFalloff;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    float light = max(dot(normal, -lightDir), 0.0);

    vec3 baseColor = texture(uTexture, vUV).rgb;
    float ambient = 0.2;
    vec3 litColor = baseColor * (ambient + light * (1.0 - ambient));

    float fogRange = max(uFogEnd - uFogStart, 0.0001);
    float linearFog = clamp((vViewDistance - uFogStart) / fogRange, 0.0, 1.0);
    float depthDistance = max(vViewDistance - uFogStart, 0.0);
    float expFog = 1.0 - exp(-uFogDensity * depthDistance * depthDistance);

    float heightFog = 1.0;
    if (uFogHeightFalloff > 0.0001) {
        float heightAttenuation = exp(-max(vWorldPos.y, 0.0) * uFogHeightFalloff);
        heightFog = clamp(heightAttenuation, 0.0, 1.0);
    }

    float fogFactor = clamp(max(linearFog, expFog) * heightFog, 0.0, 1.0);
    vec3 color = mix(litColor, uFogColor, fogFactor);
    FragColor = vec4(color, 1.0);
}
