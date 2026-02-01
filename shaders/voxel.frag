#version 450 core

in vec3 vNormal;
in vec2 vUV;
in float vSunlight;
in float vEmissive;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform sampler2D uTexture;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    float light = max(dot(normal, -lightDir), 0.0);
    float sunlight = clamp(vSunlight, 0.0, 1.0);
    float emissive = clamp(vEmissive, 0.0, 1.0);

    vec3 baseColor = texture(uTexture, vUV).rgb;
    float ambient = mix(0.05, 0.2, sunlight);
    float diffuse = light * sunlight;
    vec3 litColor = baseColor * (ambient + diffuse * (1.0 - ambient));
    vec3 emissiveColor = baseColor * emissive;
    vec3 color = max(litColor, emissiveColor);
    FragColor = vec4(color, 1.0);
}
