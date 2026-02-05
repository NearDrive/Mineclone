#version 330 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneColor;
uniform int uCurvatureEnabled;
uniform float uCurvatureStrength;

void main() {
    vec2 uv = vTexCoord;

    if (uCurvatureEnabled != 0) {
        vec2 centered = uv * 2.0 - 1.0;
        // Vertical-only bend keeps horizon subtly curved while minimizing side distortion.
        float bend = centered.x * centered.x * uCurvatureStrength;
        centered.y -= bend;
        uv = centered * 0.5 + 0.5;
    }

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.08, 0.10, 0.15, 1.0);
    } else {
        FragColor = texture(uSceneColor, uv);
    }
}
