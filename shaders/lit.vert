#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
    vec4 lightPosAndAmbient;
    vec4 eyePosAndShininess;
    vec4 materialDiffuseSpec;
} pc;

layout(location = 0) out vec3 fragNormalWS;
layout(location = 1) out vec3 fragPosWS;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    // mat3(model) ignora escala no uniforme (deforma la normal un poco en
    // ese caso) -- simplificacion deliberada de esta fase, ver LitPipeline.h.
    fragNormalWS = normalize(mat3(pc.model) * inNormal);
    fragPosWS = worldPos.xyz;
}
