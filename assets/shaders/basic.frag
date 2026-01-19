#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec3 VertexColor;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform vec3 uObjectColor;
uniform bool uUseVertexColor;
uniform vec3 uSkyColor;
uniform vec3 uGroundColor;
uniform float uAmbientStrength;
uniform bool uUseFog;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uBottomTintColor;
uniform float uBottomTintStrength;

out vec4 FragColor;

void main()
{
    // 半球环境光照（天空/地面）
    float hemi = clamp(normalize(Normal).y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemiColor = mix(uGroundColor, uSkyColor, hemi);
    vec3 ambient = uAmbientStrength * hemiColor;
    
    // 漫反射光照（方向光）
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    
    // 镜面反射光照（Blinn-Phong）
    float specularStrength = 0.35;
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * uLightColor;
    
    vec3 baseColor = uUseVertexColor ? VertexColor : uObjectColor;
    // 最终颜色
    vec3 result = (ambient + diffuse + specular) * baseColor;

    // 底部区域色调（用于船底与水面色差融合）
    if (uBottomTintStrength > 0.0) {
        float bottomMask = smoothstep(-0.05, -0.8, norm.y);
        vec3 bottomTarget = mix(result, uBottomTintColor, 0.6);
        result = mix(result, bottomTarget, bottomMask * uBottomTintStrength);
    }

    // 轻雾效
    if (uUseFog) {
        float dist = length(uViewPos - FragPos);
        float fogFactor = exp(-uFogDensity * dist);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        result = mix(uFogColor, result, fogFactor);
    }

    FragColor = vec4(result, 1.0);
}
