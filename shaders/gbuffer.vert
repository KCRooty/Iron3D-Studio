#version 450

// Pre-pasada de profundidad+normales en espacio de vista (Fase 3, SSAO --
// ver ROADMAP_RENDER.md): mismo recorrido de malla que lit.vert, pero
// escribe posicion y normal en espacio de VISTA (no de mundo) para que
// ssao.frag pueda comparar profundidades sin tener que deshacer la
// proyeccion de la camara.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
} pc;

layout(location = 0) out vec3 outViewPos;
layout(location = 1) out vec3 outViewNormal;

void main() {
    vec4 viewPos4 = pc.view * pc.model * vec4(inPosition, 1.0);
    outViewPos = viewPos4.xyz;
    // mat3(view*model) ignora escala no uniforme, misma simplificacion
    // deliberada que ya usa lit.vert con mat3(model).
    outViewNormal = mat3(pc.view * pc.model) * inNormal;
    gl_Position = pc.proj * viewPos4;
}
