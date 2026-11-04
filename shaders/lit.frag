#version 450

layout(location = 0) in vec3 fragNormalWS;
layout(location = 1) in vec3 fragPosWS;
layout(location = 2) in vec4 fragPosLightSpace;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
    vec4 lightPosAndAmbient;    // xyz = posicion de luz, w = intensidad ambiente (hack, sin GI real)
    vec4 eyePosAndRoughness;    // xyz = posicion de camara, w = roughness [0,1]
    vec4 baseColorAndMetallic;  // xyz = color base (albedo), w = metallic [0,1]
    mat4 lightViewProj;         // Fase render 2: igual matriz que uso ShadowMap para dibujar el mapa
} pc;

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
// Fase 3 (SSAO): textura de oclusion ambiental ya difuminada, a la misma
// resolucion que el framebuffer -- se indexa con gl_FragCoord en vez de un
// UV propio (no hay vertex shader que la traiga) via textureSize, igual
// patron que el texel de shadowFactor() de abajo.
layout(set = 1, binding = 0) uniform sampler2D aoMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz (distribucion de normales) + Smith-Schlick-GGX
// (geometria/sombreado-oclusion) + Fresnel-Schlick -- la combinacion
// estandar de cualquier renderer PBR moderno (metodo publico documentado en
// "Real Shading in Unreal Engine 4", Karis 2013; no es codigo de Maxon).
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // remapeo "directo" de Karis (no IBL)
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-6);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Aproximacion barata de IBL sin cubemap ni texturas: "hemisphere ambient"
// (un color solido para el cielo arriba y otro para el suelo abajo),
// muestreada por la normal para el termino difuso y por el vector de
// reflexion para el especular, mas la aproximacion polinomica de Karis para
// la integral split-sum del BRDF de entorno ("Real Shading in Unreal Engine
// 4", Karis 2013/2014 -- tecnica generica y publica, no viene de Maxon).
// Sustituye el hack plano "ambient = ambientStrength * baseColor" de antes:
// con ese approach un metal muy pulido (diffuse casi anulado, lobulo
// especular GGX casi una aguja) se veia practicamente negro fuera del punto
// exacto de brillo, por no tener nada que reflejar. Esto no es IBL real
// basado en HDRI (eso es fase futura, ver ROADMAP_RENDER.md) pero da un
// entorno minimo y barato que reflejar sin tocar push constants ni anadir
// samplers.
const vec3 kSkyColor = vec3(0.55, 0.60, 0.72);
const vec3 kGroundColor = vec3(0.05, 0.045, 0.04);

vec3 hemisphereAmbient(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(kGroundColor, kSkyColor, t);
}

// Karis, aproximacion movil de la integral split-sum del BRDF de entorno
// (ajuste polinomico; evita necesitar una textura LUT precalculada).
vec2 envBRDFApprox(float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// PCF 3x3 manual con sesgo dependiente de la pendiente (NdotL) para evitar
// "shadow acne" -- patron estandar de cualquier tutorial de shadow mapping
// (p.ej. LearnOpenGL, "Shadow Mapping"), generico y publico, no viene de
// Maxon. fragPosLightSpace ya trae la misma vista*proyeccion de la luz que
// uso ShadowMap para rellenar shadowMap (ver ShadowMap.cpp/shadow.vert), asi
// que basta con la division de perspectiva + remapeo a UV [0,1]. Fuera del
// frustum ortografico de la luz, el sampler usa CLAMP_TO_BORDER con borde
// blanco (profundidad 1.0 = "lejos"), asi que esos pixeles nunca quedan en
// sombra -- no hace falta un caso especial para "sin luz"/fuera de rango.
float shadowFactor(vec4 posLightSpace, float NdotL) {
    vec3 proj = posLightSpace.xyz / posLightSpace.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    float currentDepth = proj.z;
    if (currentDepth > 1.0) return 0.0; // mas alla del plano lejano de la luz: no se puede saber, no ensombrece

    const float minBias = 0.0006;
    const float maxBias = 0.0025;
    float bias = max(maxBias * (1.0 - NdotL), minBias);

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closestDepth = texture(shadowMap, uv + vec2(x, y) * texel).r;
            shadow += (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// Cook-Torrance: una sola luz puntual blanca, sin sombras ni atenuacion por
// distancia ni IBL/GI todavia (Fase 5, ver ROADMAP_RENDER.md). El termino
// "ambient" sigue siendo el mismo hack plano de antes -- aqui no hay nada
// que lo sustituya hasta que haya sombras/oclusion de verdad.
void main() {
    vec3 N = normalize(fragNormalWS);
    vec3 lightPos = pc.lightPosAndAmbient.xyz;
    float ambientStrength = pc.lightPosAndAmbient.w;
    vec3 eyePos = pc.eyePosAndRoughness.xyz;
    float roughness = clamp(pc.eyePosAndRoughness.w, 0.045, 1.0); // evita a=0 (GGX degenerado)
    vec3 baseColor = pc.baseColorAndMetallic.xyz;
    float metallic = clamp(pc.baseColorAndMetallic.w, 0.0, 1.0);

    vec3 L = normalize(lightPos - fragPosWS);
    vec3 V = normalize(eyePos - fragPosWS);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // F0: dielectricos ~0.04 (plastico/piedra/madera...), metales usan su
    // propio albedo como color de reflejo especular.
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(HdotV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

    // Conservacion de energia: lo que no se refleja especularmente se
    // difunde -- y los metales no difunden nada (toda la luz que no
    // rebota especularmente la absorben).
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * baseColor / PI;

    // Hemisferio ambiente (ver hemisphereAmbient arriba): difuso + especular
    // aproximados, con la misma conservacion de energia (kD/F) que la luz
    // directa de arriba -- asi un metal pulido refleja el "cielo" en vez de
    // verse negro, y un dielectrico mate sigue recibiendo su irradiancia
    // difusa normal.
    vec3 R = reflect(-V, N);
    vec3 kSAmbient = fresnelSchlick(NdotV, F0);
    vec3 kDAmbient = (vec3(1.0) - kSAmbient) * (1.0 - metallic);
    vec3 irradiance = hemisphereAmbient(N);
    vec3 diffuseAmbient = irradiance * baseColor * kDAmbient;

    // El entorno muestreado por reflexion se difumina hacia la irradiancia
    // (mas plana) segun sube la rugosidad -- aproxima el prefiltrado por
    // mip-roughness de un cubemap real, sin tener uno.
    vec3 prefiltered = mix(hemisphereAmbient(R), irradiance, roughness);
    vec2 envBRDF = envBRDFApprox(roughness, NdotV);
    vec3 specularAmbient = prefiltered * (F0 * envBRDF.x + vec3(envBRDF.y));

    // Sombra (Fase render 2): solo atenua la luz DIRECTA, igual que
    // cualquier shadow map basico -- el ambiente (arriba) representa luz
    // rebotada/de entorno, que no tiene por que faltar solo porque la luz
    // principal este tapada.
    float shadow = shadowFactor(fragPosLightSpace, NdotL);

    // Fase 3: la oclusion ambiental solo atenua el termino ambiente/de
    // entorno (luz indirecta aproximada) -- la luz DIRECTA ya tiene su
    // propia sombra de arriba, igual separacion que hace esa misma sombra
    // entre "directa" y "ambiente".
    float ao = texture(aoMap, gl_FragCoord.xy / vec2(textureSize(aoMap, 0))).r;

    const vec3 lightColor = vec3(1.0);
    vec3 ambient = ambientStrength * (diffuseAmbient + specularAmbient) * ao;
    vec3 color = ambient + (diffuse + specular) * lightColor * NdotL * (1.0 - shadow);

    // Tonemap Reinhard + correccion gamma: sin esto, cualquier zona con
    // especular fuerte (sobre todo en metales/roughness bajo) satura a
    // blanco plano de forma mucho mas agresiva que con el Phong anterior
    // (el lobulo GGX concentra mucha mas energia en un area mas pequena).
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
