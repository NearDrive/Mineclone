#version 450 core

in vec3 vNormal;

out vec4 FragColor;

uniform vec3 uLightDir;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    float light = max(dot(normal, -lightDir), 0.0);

    vec3 baseColor = vec3(0.55, 0.65, 0.5);
    float ambient = 0.2;
    vec3 color = baseColor * (ambient + light * (1.0 - ambient));
    FragColor = vec4(color, 1.0);
}
