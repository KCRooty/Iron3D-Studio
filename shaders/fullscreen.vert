#version 450

// Vertex shader comun a las pasadas de pantalla completa (SSAO y blur, Fase
// 3): triangulo unico que cubre toda la pantalla calculado a partir de
// gl_VertexIndex, sin vertex buffer (truco estandar, publico y generico --
// no viene de Maxon). dibujar con vkCmdDraw(cmd, 3, 1, 0, 0) y ningun
// binding de vertices.

layout(location = 0) out vec2 outUV;

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
