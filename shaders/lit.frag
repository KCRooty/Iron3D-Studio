#version 450

layout(location = 0) in vec3 fragNormalWS;
layout(location = 1) in vec3 fragPosWS;
layout(location = 2) in vec4 fragPosLightSpace;
layout(location = 3) in vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
    vec4 flags;                 // x = indicadores de textura (ver kTex* abajo)
    vec4 eyePosAndRoughness;    // xyz = posicion de camara, w = roughness [0,1]
    vec4 baseColorAndMetallic;  // xyz = color base (albedo), w = metallic [0,1]
    vec4 uvTransform;           // 2026-11-10: uv de la imagen = uv * xy + zw (Etiqueta de Textura)
    vec4 emission;              // xyz = Luminancia del material (color * brillo)
} pc;

// Indicadores de flags.x (entero guardado como float).
const int kTexColor = 1; // hay textura de Color: sustituye al color base, como en C4D
const int kTexTile = 2;  // Mosaico activo; sin el, fuera de 0..1 se ve el color base

// Luces de la escena (2026-11-10, ver LightsUniform.h / SceneLighting.h).
// Sin luces (info.x == 0) se usa la luz por defecto: una puntual blanca en la
// camara, como hasta ahora.
struct Light {
    vec4 posType;     // xyz posicion, w tipo (0 puntual, 1 foco, 2 infinita)
    vec4 dirFalloff;  // xyz direccion de emision, w modo de caida de C4D
    vec4 colorRadius; // xyz color * intensidad, w radio de caida
    vec4 cone;        // x cos exterior, y cos interior, z radio interior, w 1 difusion | 2 especular
};
layout(set = 3, binding = 0, std140) uniform SceneLights {
    mat4 lightViewProj; // la del mapa de sombras (la usa lit.vert)
    vec4 info;          // x numero de luces, y luz con sombra (-1 = ninguna), z intensidad ambiente
    Light lights[32];
} sl;

layout(set = 0, binding = 0) uniform sampler2D shadowMap;
// Fase 3 (SSAO): textura de oclusion ambiental ya difuminada, a la misma
// resolucion que el framebuffer -- se indexa con gl_FragCoord en vez de un
// UV propio (no hay vertex shader que la traiga) via textureSize, igual
// patron que el texel de shadowFactor() de abajo.
layout(set = 1, binding = 0) uniform sampler2D aoMap;
// 2026-11-10: textura de Color del material (sRGB: el muestreo ya devuelve
// lineal). Sin textura se enlaza una blanca de 1x1 y kTexColor va apagado.
layout(set = 2, binding = 0) uniform sampler2D colorTex;

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

/// Atenuacion por distancia de C4D (mismas formulas que falloffFactor en
// SceneLighting.cpp): 0 ninguna, 5 escalon, 6/9 inversa (con/sin tope),
// 7/10 cuadrado inverso (con/sin tope), 8 lineal.
float falloffFactor(int mode, float d, float radius, float innerRadius) {
    radius = max(radius, 1e-4);
    d = max(d, 1e-4);
    if (mode == 5) return d <= radius ? 1.0 : 0.0;
    if (mode == 6) return min(1.0, radius / d);
    if (mode == 7) return min(1.0, (radius / d) * (radius / d));
    if (mode == 8) {
        if (d <= innerRadius) return 1.0;
        return clamp((radius - d) / max(radius - innerRadius, 1e-4), 0.0, 1.0);
    }
    if (mode == 9) return radius / d;
    if (mode == 10) return (radius / d) * (radius / d);
    return 1.0;
}

// Cook-Torrance por luz (GGX + Smith + Fresnel-Schlick), sumando todas las
// luces de la escena (2026-11-10). `radiance` = color * intensidad de la luz
// ya atenuado; se multiplica por PI para que una luz al 100 % sobre una cara
// de frente devuelva el color del material, como en el visor de C4D.
// `lflags`: 1 = la luz aporta difusion, 2 = especular (General de la Luz de C4D).
vec3 shadeLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 baseColor, vec3 F0, float metallic, float roughness,
                int lflags) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(HdotV, F0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    // Conservacion de energia: lo que no se refleja especularmente se
    // difunde, y los metales no difunden nada.
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * baseColor / PI;
    if ((lflags & 1) == 0) diffuse = vec3(0.0);
    if ((lflags & 2) == 0) specular = vec3(0.0);
    return (diffuse + specular) * radiance * PI * NdotL;
}

// Respuesta de pantalla: lineal hasta 0.8 y hombro suave hasta 1 (el visor
// de C4D no comprime los medios tonos; el Reinhard de antes oscurecia todo).
vec3 softClip(vec3 c) {
    vec3 knee = vec3(0.8);
    vec3 over = max(c - knee, vec3(0.0));
    return min(c, knee) + 0.2 * (vec3(1.0) - exp(-over / 0.2));
}

void main() {
    vec3 N = normalize(fragNormalWS);
    int flags = int(pc.flags.x + 0.5);
    vec3 eyePos = pc.eyePosAndRoughness.xyz;
    float roughness = clamp(pc.eyePosAndRoughness.w, 0.045, 1.0); // evita a=0 (GGX degenerado)
    vec3 baseColor = pc.baseColorAndMetallic.xyz;
    if ((flags & kTexColor) != 0) {
        vec2 uv = fragUV * pc.uvTransform.xy + pc.uvTransform.zw;
        // Se muestrea siempre (fuera del if por pixel): las derivadas que
        // eligen el mip tienen que calcularse con todo el cuadro 2x2 activo.
        vec3 texel = texture(colorTex, uv).rgb;
        bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
        if ((flags & kTexTile) != 0 || inside) baseColor = texel;
    }
    float metallic = clamp(pc.baseColorAndMetallic.w, 0.0, 1.0);

    vec3 V = normalize(eyePos - fragPosWS);
    float NdotV = max(dot(N, V), 1e-4);
    // Las caras traseras se iluminan como las delanteras (mallas sin cerrar
    // de modelos importados: C4D tampoco las deja negras).
    if (dot(N, V) < 0.0) { N = -N; NdotV = max(dot(N, V), 1e-4); }

    // F0: dielectricos ~0.04 (plastico/piedra/madera...), metales usan su
    // propio albedo como color de reflejo especular.
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // Luz directa: todas las luces de la escena, o la luz por defecto.
    vec3 direct = vec3(0.0);
    int count = int(sl.info.x + 0.5);
    int shadowLight = int(floor(sl.info.y + 0.5));
    if (count == 0) {
        vec3 L = normalize(eyePos - fragPosWS);
        direct = shadeLight(N, V, L, vec3(1.0), baseColor, F0, metallic, roughness, 3);
    }
    for (int i = 0; i < count && i < 32; ++i) {
        Light li = sl.lights[i];
        int type = int(li.posType.w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (type == 2) {
            L = -normalize(li.dirFalloff.xyz);
        } else {
            vec3 toLight = li.posType.xyz - fragPosWS;
            float d = length(toLight);
            L = toLight / max(d, 1e-6);
            atten = falloffFactor(int(li.dirFalloff.w + 0.5), d, li.colorRadius.w, li.cone.z);
            if (type == 1) {
                float cosAngle = dot(-L, normalize(li.dirFalloff.xyz));
                atten *= smoothstep(li.cone.x, li.cone.y, cosAngle);
            }
        }
        if (atten <= 0.0) continue;
        vec3 c = shadeLight(N, V, L, li.colorRadius.xyz * atten, baseColor, F0, metallic, roughness,
                            int(li.cone.w + 0.5));
        // Sombra (Fase render 2): solo la luz que hace el mapa de sombras.
        if (i == shadowLight) c *= 1.0 - shadowFactor(fragPosLightSpace, max(dot(N, L), 0.0));
        direct += c;
    }

    // Hemisferio ambiente (ver hemisphereAmbient arriba): difuso + especular
    // aproximados, con la misma conservacion de energia (kD/F) que la luz
    // directa -- asi un metal pulido refleja el "cielo" en vez de verse negro.
    vec3 R = reflect(-V, N);
    vec3 kSAmbient = fresnelSchlick(NdotV, F0);
    vec3 kDAmbient = (vec3(1.0) - kSAmbient) * (1.0 - metallic);
    vec3 irradiance = hemisphereAmbient(N);
    vec3 diffuseAmbient = irradiance * baseColor * kDAmbient;
    // El entorno muestreado por reflexion se difumina hacia la irradiancia
    // segun sube la rugosidad -- aproxima el prefiltrado de un cubemap real.
    vec3 prefiltered = mix(hemisphereAmbient(R), irradiance, roughness);
    vec2 envBRDF = envBRDFApprox(roughness, NdotV);
    vec3 specularAmbient = prefiltered * (F0 * envBRDF.x + vec3(envBRDF.y));

    // Fase 3: la oclusion ambiental solo atenua el termino ambiente.
    float ao = texture(aoMap, gl_FragCoord.xy / vec2(textureSize(aoMap, 0))).r;
    vec3 ambient = sl.info.z * (diffuseAmbient + specularAmbient) * ao;

    // Luminancia del material: se suma tal cual, no depende de las luces.
    vec3 color = ambient + direct + pc.emission.xyz;

    color = softClip(color);
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
