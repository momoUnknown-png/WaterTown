#version 330 core
in vec3 vDir;
out vec4 FragColor;

void main() {
    vec3 dir = normalize(vDir);
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    t = pow(t, 0.65);

    vec3 horizon = vec3(0.90, 0.95, 1.0);
    vec3 zenith = vec3(0.15, 0.35, 0.75);
    vec3 color = mix(horizon, zenith, t);

    FragColor = vec4(color, 1.0);
}
