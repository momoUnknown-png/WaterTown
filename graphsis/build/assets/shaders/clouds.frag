#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform float uAlpha;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    vec2 uv = vUV;
    vec2 center = vec2(0.5);
    float dist = length(uv - center);

    float soft = smoothstep(0.7, 0.1, dist);
    float noise = rand(uv * 4.0) * 0.5 + 0.75;
    float alpha = soft * noise * uAlpha * 1.8;

    vec3 color = vec3(1.0, 1.0, 1.0);
    FragColor = vec4(color, alpha);
}
