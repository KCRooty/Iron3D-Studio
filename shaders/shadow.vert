#version 450

// Pasada de profundidad desde el punto de vista de la luz (Fase render 2,
// ver ROADMAP_RENDER.md): el unico proposito es escribir gl_Position en el
// mapa de sombras -- sin fragment shader (pipeline solo-vertice, ver
// ShadowPipeline), nada que leer en color.

layout(location = 0) in vec3 inPosition;
// La normal (location 1) vive en el mismo buffer de Vertex que usa lit.vert,
// pero no hace falta declararla aqui -- el stride del binding (ver
// ShadowPipeline::create) sigue siendo sizeof(Vertex) para que coincida con
// el buffer real, el atributo simplemente no se lee.

layout(push_constant) uniform PushConstants {
    mat4 lightMvp;
} pc;

void main() {
    gl_Position = pc.lightMvp * vec4(inPosition, 1.0);
}
