#version 450

// Oclusion ambiental en espacio de pantalla (Fase 3, ver ROADMAP_RENDER.md):
// tecnica estandar de hemisferio con 16 muestras (Crysis 2007 / tutorial de
// SSAO de LearnOpenGL -- generica y publica, no viene de Maxon), adaptada
// para no necesitar ninguna textura de ruido: la rotacion por pixel sale de
// un hash de gl_FragCoord en vez de una textura 4x4 de vectores aleatorios,
// asi esta pasada no necesita subir ningun recurso nuevo aparte del propio
// G-buffer. Opera entera en espacio de VISTA (gPos/gNormal, ver gbuffer.frag).

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D gPos;
layout(set = 0, binding = 1) uniform sampler2D gNormal;

layout(push_constant) uniform PushConstants {
    mat4 proj;   // misma proyeccion que uso el G-buffer -- hace falta para
                 // reproyectar cada muestra del kernel a espacio de pantalla.
    vec4 params; // x=radio (unidades de escena), y=bias, z=exponente de intensidad, w=sin uso
} pc;

layout(location = 0) out float outAo;

const int kKernelSize = 16;
// Kernel de hemisferio precalculado fuera de linea (espiral de angulo
// dorado + sesgo cuadratico hacia el origen, igual idea que el kernel
// aleatorio del tutorial de LearnOpenGL pero generado de forma
// deterministica -- evita depender de un PRNG en GLSL). z >= 0 siempre:
// se orienta hacia la normal real del fragmento via la TBN de abajo.
const vec3 kKernel[kKernelSize] = vec3[](
    vec3(0.1035, 0.0000, 0.0032), vec3(-0.0838, 0.0767, 0.0107),
    vec3(0.0114, -0.1295, 0.0206), vec3(0.0926, 0.1211, 0.0342),
    vec3(-0.1775, -0.0313, 0.0528), vec3(0.1796, -0.1140, 0.0779),
    vec3(-0.0652, 0.2401, 0.1106), vec3(-0.1314, -0.2552, 0.1523),
    vec3(0.3062, 0.1118, 0.2043), vec3(-0.3357, 0.1385, 0.2681),
    vec3(0.1689, -0.3588, 0.3448), vec3(0.1254, 0.4023, 0.4357),
    vec3(-0.3753, -0.2169, 0.5423), vec3(0.4141, -0.0908, 0.6656),
    vec3(-0.2169, 0.3079, 0.8075), vec3(-0.0318, -0.2460, 0.9688)
);

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123);
}

void main() {
    vec3 fragPos = texture(gPos, inUV).xyz;
    vec3 N = texture(gNormal, inUV).xyz;
    // w=0 en los dos attachments es el valor de "clear" (ver SsaoPass.cpp) --
    // significa que aqui no se dibujo ninguna malla este frame (fuera del
    // rectangulo del visor, o fondo); sin geometria no hay nada que ocluir.
    if (dot(N, N) < 1e-6) { outAo = 1.0; return; }
    N = normalize(N);

    float angle = hash12(gl_FragCoord.xy) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = normalize(randomVec - N * dot(randomVec, N));
    vec3 bitangent = cross(N, tangent);
    mat3 TBN = mat3(tangent, bitangent, N);

    const float radius = pc.params.x;
    const float bias = pc.params.y;
    float occlusion = 0.0;
    for (int i = 0; i < kKernelSize; ++i) {
        vec3 samplePos = fragPos + (TBN * kKernel[i]) * radius;

        vec4 offset = pc.proj * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        float sampleDepth = texture(gPos, offset.xy).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(fragPos.z - sampleDepth), 1e-4));
        // Vista mirando hacia -Z (convencion estandar de glm::lookAt): mas
        // cerca de la camara = Z menos negativo. Si lo que hay de verdad en
        // ese pixel esta MAS cerca que la muestra del kernel, algo la ocluye.
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(kKernelSize));
    outAo = pow(clamp(ao, 0.0, 1.0), pc.params.z);
}
