#version 450

// Escribe el G-buffer minimo que necesita SSAO (Fase 3): posicion y normal
// en espacio de vista, en dos attachments de color separados. Sin
// iluminacion ni material -- esto no se ve nunca en pantalla directamente.

layout(location = 0) in vec3 inViewPos;
layout(location = 1) in vec3 inViewNormal;

layout(location = 0) out vec4 outViewPos;
layout(location = 1) out vec4 outViewNormal;

void main() {
    outViewPos = vec4(inViewPos, 1.0);
    outViewNormal = vec4(normalize(inViewNormal), 0.0);
}
