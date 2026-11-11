#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV; // 2026-11-10: texturas en el visor

// Igual bloque que lit.frag (ver alli el significado de cada campo).
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
    vec4 flags;
    vec4 eyePosAndRoughness;
    vec4 baseColorAndMetallic;
    vec4 uvTransform;
    vec4 emission;
} pc;

// Luces de la escena (2026-11-10, ver LightsUniform.h): aqui solo hace falta
// la matriz del mapa de sombras.
struct Light {
    vec4 posType;
    vec4 dirFalloff;
    vec4 colorRadius;
    vec4 cone;
};
layout(set = 3, binding = 0, std140) uniform SceneLights {
    mat4 lightViewProj;
    vec4 info;
    Light lights[32];
} sl;

layout(location = 0) out vec3 fragNormalWS;
layout(location = 1) out vec3 fragPosWS;
layout(location = 2) out vec4 fragPosLightSpace;
layout(location = 3) out vec2 fragUV;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    // mat3(model) ignora escala no uniforme (deforma la normal un poco en
    // ese caso) -- simplificacion deliberada de esta fase, ver LitPipeline.h.
    fragNormalWS = normalize(mat3(pc.model) * inNormal);
    fragPosWS = worldPos.xyz;
    fragPosLightSpace = sl.lightViewProj * worldPos;
    fragUV = inUV;
}
