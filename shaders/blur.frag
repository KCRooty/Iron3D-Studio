#version 450

// Blur de caja 4x4 sobre el resultado crudo de ssao.frag (Fase 3): hace
// falta porque la rotacion por pixel de ssao.frag es ruido continuo (ver
// hash12 ahi), no una textura de 4x4 repetida -- sin suavizar, el patron de
// rotacion queda visible como grano. Tecnica generica estandar (mismo
// tamano de kernel que el tutorial de SSAO de LearnOpenGL), no hace falta
// nada mas elaborado para la escena de un puñado de primitivas de Iron3D.

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D aoRaw;

layout(location = 0) out float outAo;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(aoRaw, 0));
    float sum = 0.0;
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            sum += texture(aoRaw, inUV + vec2(x, y) * texel).r;
        }
    }
    outAo = sum / 16.0;
}
