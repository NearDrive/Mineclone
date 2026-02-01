#version 450 core

in vec3 vNormal;
in vec2 vUV;
in float vSunlight;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform sampler2D uTexture;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    float light = max(dot(normal, -lightDir), 0.0);
    float sunlight = clamp(vSunlight, 0.0, 1.0);

    vec3 baseColor = texture(uTexture, vUV).rgb;
    float ambient = mix(0.05, 0.2, sunlight);
    float diffuse = light * sunlight;
    vec3 color = baseColor * (ambient + diffuse * (1.0 - ambient));
    FragColor = vec4(color, 1.0);
}
