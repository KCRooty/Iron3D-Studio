#version 450

layout(location = 0) in vec3 fragNormalWS;
layout(location = 1) in vec3 fragPosWS;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
    vec4 lightPosAndAmbient;   // xyz = posicion de luz, w = intensidad ambiente
    vec4 eyePosAndShininess;   // xyz = posicion de camara, w = exponente Phong
    vec4 materialDiffuseSpec;  // xyz = color difuso, w = intensidad especular
} pc;

layout(location = 0) out vec4 outColor;

// Phong clasico: ambiente + difuso (Lambert) + especular. Una sola luz
// blanca puntual, sin sombras ni atenuacion por distancia todavia -- Fase 5
// (iluminacion basica), no PBR.
void main() {
    vec3 N = normalize(fragNormalWS);
    vec3 lightPos = pc.lightPosAndAmbient.xyz;
    float ambientStrength = pc.lightPosAndAmbient.w;
    vec3 eyePos = pc.eyePosAndShininess.xyz;
    float shininess = max(pc.eyePosAndShininess.w, 1.0);
    vec3 diffuseColor = pc.materialDiffuseSpec.xyz;
    float specularStrength = pc.materialDiffuseSpec.w;

    vec3 L = normalize(lightPos - fragPosWS);
    vec3 V = normalize(eyePos - fragPosWS);
    vec3 R = reflect(-L, N);

    const vec3 lightColor = vec3(1.0);

    vec3 ambient = ambientStrength * diffuseColor;
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * diffuseColor * lightColor;
    float spec = (diff > 0.0) ? pow(max(dot(R, V), 0.0), shininess) : 0.0;
    vec3 specular = specularStrength * spec * lightColor;

    outColor = vec4(ambient + diffuse + specular, 1.0);
}
