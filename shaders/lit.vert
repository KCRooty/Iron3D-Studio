#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV; // 2026-11-10: texturas en el visor

// Igual bloque que lit.frag (ver alli el significado de cada campo).
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
    vec4 lightPosAndFlags;
    vec4 eyePosAndRoughness;
    vec4 baseColorAndMetallic;
    mat4 lightViewProj; // Fase render 2: posicion del fragmento en espacio de la luz
    vec4 uvTransform;
} pc;

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
    fragPosLightSpace = pc.lightViewProj * worldPos;
    fragUV = inUV;
}
