#version 450 core

in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform sampler2D uTexture;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    float light = max(dot(normal, -lightDir), 0.0);

    vec3 baseColor = texture(uTexture, vUV).rgb;
    float ambient = 0.2;
    vec3 color = baseColor * (ambient + light * (1.0 - ambient));
    FragColor = vec4(color, 1.0);
}
