#pragma once

// Escena de referencia para las pruebas de ida y vuelta (.iron y .c4d) y un
// comparador que dice exactamente que valor no ha vuelto igual. Solo core:
// la usan iron3d_tests (todas las plataformas) y c4d_roundtrip (Cineware).

#include "core/Material.h"
#include "core/SceneNode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace iron3d::testio {

// Malla con UV: un cuadrado (2 triangulos que forman un cuadrilatero) y un
// triangulo suelto, con UV distintas en cada esquina.
inline MeshData uvMesh() {
    MeshData m;
    const glm::vec3 n(0.0f, 1.0f, 0.0f), c(0.8f);
    auto v = [&](float x, float z, float u, float w) {
        Vertex out{glm::vec3(x, 0.0f, z), c, n};
        out.uv = glm::vec2(u, w);
        return out;
    };
    m.vertices = {v(-0.5f, -0.5f, 0.0f, 0.0f), v(-0.5f, 0.5f, 0.0f, 1.0f), v(0.5f, 0.5f, 1.0f, 1.0f), v(0.5f, -0.5f, 1.0f, 0.0f),
                  v(1.0f, 0.0f, 0.25f, 0.5f),  v(1.0f, 1.0f, 0.75f, 0.5f), v(2.0f, 0.5f, 0.5f, 0.125f)};
    m.indices = {0, 1, 2, 0, 2, 3, 4, 5, 6};
    updateBounds(m);
    return m;
}

// `texDir`: carpeta donde estan (o se dan por buenas) las texturas.
inline void buildScene(SceneNode& root, std::vector<Material>& mats, const std::string& texDir) {
    mats.clear();
    Material a;
    a.name = "Plástico \"rojo\" 100%";
    a.baseColor = {0.8f, 0.1f, 0.05f};
    a.roughness = 0.35f;
    a.emission = {0.2f, 0.1f, 0.0f};
    a.transparency = {0.25f, 0.25f, 0.25f};
    a.ior = 1.33f;
    a.normalStrength = 0.75f;
    a.bumpStrength = 0.4f;
    a.texture(TexSlot::Color) = "tex/color.png";
    a.texture(TexSlot::Normal) = "tex/normal.png";
    a.texture(TexSlot::Bump) = "tex/bump.png";
    a.texture(TexSlot::Alpha) = "tex/alpha.png";
    a.texture(TexSlot::Emission) = "tex/emit.png";
    a.texture(TexSlot::Roughness) = "tex/rough.png";
    a.texBaseDir = texDir;
    mats.push_back(a);
    Material metal;
    metal.name = "Oro";
    metal.baseColor = {1.0f, 0.766f, 0.336f};
    metal.metallic = 1.0f;
    metal.roughness = 0.2f;
    mats.push_back(metal);
    Material layered; // capas "de C4D": tienen que volver tal cual
    layered.name = "Capas";
    layered.baseColor = {0.2f, 0.4f, 0.6f};
    ReflectLayer l1;
    l1.name = "Barniz";
    l1.distribution = 3;
    l1.fresnel = 1;
    l1.fresnelPreset = 0;
    l1.strength = 0.9f;
    l1.specular = 0.3f;
    l1.roughness = 0.05f;
    l1.ior = 1.6f;
    ReflectLayer l2;
    l2.name = "Base";
    l2.distribution = 2;
    l2.fresnel = 2;
    l2.strength = 0.5f;
    l2.roughness = 0.6f;
    l2.color = {0.9f, 0.5f, 0.2f};
    layered.layers = {l1, l2};
    pbrFromLayers(layered.layers, true, layered.baseColor, layered.metallic, layered.roughness);
    mats.push_back(layered);

    SceneNode* grupo = root.addChild("Grupo");
    grupo->position = {1.5f, -0.25f, 2.0f};
    grupo->rotationDeg = {10.0f, 30.0f, -15.0f};
    grupo->scale = {1.0f, 2.0f, 0.5f};
    grupo->editorVis = VisMode::On;
    grupo->renderVis = VisMode::Off;
    grupo->materialIndex = 0;
    grupo->texTag.projection = 6;
    grupo->texTag.offset = {0.25f, 0.5f};
    grupo->texTag.length = {0.5f, 2.0f};
    grupo->texTag.tile = true;
    grupo->texTag.seamless = true;

    SceneNode* cubo = grupo->addChild("Cubo", PrimitiveType::Cube);
    cubo->rotationDeg = {-45.0f, 120.0f, 5.0f};
    cubo->materialIndex = 1;
    cubo->animPos.set(0, {0.0f, 0.0f, 0.0f});
    cubo->animPos.set(15, {1.0f, 0.5f, -2.0f});
    cubo->animPos.set(40, {3.0f, 0.0f, 1.0f});
    cubo->animRot.set(0, {0.0f, 0.0f, 0.0f});
    cubo->animRot.set(30, {20.0f, 270.0f, -10.0f});
    cubo->animScale.set(10, {1.0f, 1.0f, 1.0f});
    cubo->animScale.set(20, {2.0f, 0.5f, 1.5f});
    cubo->animPos.keys[1].interp = KeyInterp::Linear; // tramo 15-40 lineal
    cubo->animScale.keys[0].interp = KeyInterp::Step;  // tramo 10-20 escalon

    SceneNode* malla = grupo->addChild("Malla UV", PrimitiveType::Mesh);
    malla->mesh = std::make_shared<MeshData>(uvMesh());
    malla->position = {0.0f, 1.0f, 0.0f};
    malla->materialIndex = 0; // hereda (mismo material y etiqueta que el padre)
    malla->texTag = grupo->texTag;

    SceneNode* capas = root.addChild("Esfera capas", PrimitiveType::Sphere);
    capas->materialIndex = 2;
    capas->position = {-3.0f, 0.0f, 0.0f};

    const LightKind kinds[] = {LightKind::Omni, LightKind::Spot, LightKind::Infinite, LightKind::Area};
    const char* names[] = {"Luz Omni", "Foco", "Infinita", "Área"};
    SceneNode* luces = root.addChild("Luces");
    for (int i = 0; i < 4; ++i) {
        SceneNode* l = luces->addChild(names[i], PrimitiveType::Light);
        l->position = {float(i), 3.0f, -1.0f};
        l->rotationDeg = {-60.0f, float(i * 40), 0.0f};
        l->light.kind = kinds[i];
        l->light.color = {1.0f, 0.9f - 0.1f * float(i), 0.7f};
        l->light.intensity = 0.5f + 0.25f * float(i);
        l->light.outerAngleDeg = 40.0f;
        l->light.innerAngleDeg = 12.5f;
        l->light.shadow = i % 4;
        l->light.falloff = i == 0 ? 10 : (i == 1 ? 8 : 0);
        l->light.falloffRadius = 3.75f;
        l->light.innerRadius = 0.5f;
        l->light.areaShape = i == 3 ? 0 : 1;
        l->light.areaSize = {1.5f, 0.75f, 0.25f};
    }

    SceneNode* cam = root.addChild("Cámara", PrimitiveType::Camera);
    cam->position = {0.0f, 2.0f, 8.0f};
    cam->rotationDeg = {-10.0f, 180.0f, 0.0f};
    cam->camera.focalMm = 50.0f;
    cam->camera.sensorMm = 24.0f;
    cam->camera.target = "Grupo/Cubo";
    SceneNode* orto = root.addChild("Cámara orto", PrimitiveType::Camera);
    orto->camera.projection = 1;
    orto->camera.zoom = 2.5f;
}

// --- Comparacion ------------------------------------------------------------------

struct Diffs {
    std::vector<std::string> list;
    void add(const std::string& where, const std::string& what) { list.push_back(where + ": " + what); }
};

inline bool nearF(float a, float b, float tol) { return std::fabs(a - b) <= tol * std::max(1.0f, std::fabs(a)); }
inline std::string num(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

struct CompareOptions {
    float tol = 0.0f;           // 0 = exacto
    bool meshExact = true;      // false: se comparan las esquinas sin orden (el .c4d reordena)
    bool layersFromPbr = false; // materiales sin capas en origen: las del destino son generadas
    bool rotationEquivalent = false; // HPB: basta con la misma orientacion
    bool primitivesBecomeMesh = false; // Cubo/Esfera/Plano vuelven como malla (al reimportar un .c4d)
};

inline void cmpF(Diffs& d, const std::string& where, const char* what, float a, float b, float tol) {
    if (!nearF(a, b, tol)) d.add(where, std::string(what) + " " + num(a) + " != " + num(b));
}
inline void cmpV(Diffs& d, const std::string& where, const char* what, const glm::vec3& a, const glm::vec3& b, float tol) {
    for (int i = 0; i < 3; ++i)
        if (!nearF(a[i], b[i], tol)) {
            d.add(where, std::string(what) + "[" + std::to_string(i) + "] " + num(a[i]) + " != " + num(b[i]));
        }
}

inline void compareMaterial(Diffs& d, const Material& a, const Material& b, const CompareOptions& o) {
    const std::string w = "material '" + a.name + "'";
    if (a.name != b.name) d.add(w, "nombre '" + b.name + "'");
    cmpV(d, w, "color", a.baseColor, b.baseColor, o.tol);
    cmpF(d, w, "metal", a.metallic, b.metallic, o.tol);
    cmpF(d, w, "rugosidad", a.roughness, b.roughness, o.tol);
    cmpV(d, w, "luminancia", a.emission, b.emission, o.tol);
    cmpV(d, w, "transparencia", a.transparency, b.transparency, o.tol);
    if (a.transparency != glm::vec3(0.0f)) cmpF(d, w, "ior", a.ior, b.ior, o.tol);
    if (!a.texture(TexSlot::Normal).empty()) cmpF(d, w, "fuerza normal", a.normalStrength, b.normalStrength, o.tol);
    if (!a.texture(TexSlot::Bump).empty()) cmpF(d, w, "fuerza relieve", a.bumpStrength, b.bumpStrength, o.tol);
    for (int i = 0; i < kTexSlotCount; ++i)
        if (a.textures[i] != b.textures[i])
            d.add(w, std::string("textura ") + texSlotName((TexSlot)i) + " '" + a.textures[i] + "' != '" + b.textures[i] + "'");
    if (o.layersFromPbr && a.layers.empty()) return;
    std::vector<ReflectLayer> la, lb;
    for (const ReflectLayer& l : a.layers) if (!l.transparencyLayer) la.push_back(l);
    for (const ReflectLayer& l : b.layers) if (!l.transparencyLayer) lb.push_back(l);
    if (la.size() != lb.size()) {
        d.add(w, "capas " + std::to_string(la.size()) + " != " + std::to_string(lb.size()));
        return;
    }
    for (size_t i = 0; i < la.size(); ++i) {
        const std::string lw = w + " capa " + std::to_string(i) + " '" + la[i].name + "'";
        if (la[i].name != lb[i].name) d.add(lw, "nombre '" + lb[i].name + "'");
        if (la[i].distribution != lb[i].distribution) d.add(lw, "distribucion " + std::to_string(lb[i].distribution));
        if (la[i].fresnel != lb[i].fresnel) d.add(lw, "fresnel " + std::to_string(lb[i].fresnel));
        if (la[i].fresnelPreset != lb[i].fresnelPreset) d.add(lw, "preset " + std::to_string(lb[i].fresnelPreset));
        cmpF(d, lw, "reflejo", la[i].strength, lb[i].strength, o.tol);
        cmpF(d, lw, "especular", la[i].specular, lb[i].specular, o.tol);
        cmpF(d, lw, "rugosidad", la[i].roughness, lb[i].roughness, o.tol);
        cmpF(d, lw, "ior", la[i].ior, lb[i].ior, o.tol);
        cmpV(d, lw, "color", la[i].color, lb[i].color, o.tol);
        if (la[i].roughnessTex != lb[i].roughnessTex) d.add(lw, "textura rugosidad '" + lb[i].roughnessTex + "'");
        if (la[i].colorTex != lb[i].colorTex) d.add(lw, "textura color '" + lb[i].colorTex + "'");
    }
}

// Esquinas de los triangulos (posicion + UV), redondeadas y ordenadas.
inline std::vector<std::array<long long, 5>> corners(const MeshData& m, float tol) {
    const double q = 1.0 / std::max(1e-6, (double)tol * 10.0);
    std::vector<std::array<long long, 5>> out;
    for (uint32_t i : m.indices) {
        const Vertex& v = m.vertices[i];
        out.push_back({std::llround(v.position.x * q), std::llround(v.position.y * q), std::llround(v.position.z * q),
                       std::llround(v.uv.x * q), std::llround(v.uv.y * q)});
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline glm::mat3 rotOf(const glm::vec3& deg) { return hpbToMat3(deg); }

inline void compareNode(Diffs& d, const SceneNode& a, const SceneNode& b, const std::vector<Material>& ma,
                        const std::vector<Material>& mb, const CompareOptions& o, const std::string& path) {
    const std::string w = path.empty() ? a.name() : path;
    if (a.name() != b.name()) d.add(w, "nombre '" + b.name() + "'");
    const bool primToMesh = o.primitivesBecomeMesh && b.primitive == PrimitiveType::Mesh &&
                            (a.primitive == PrimitiveType::Cube || a.primitive == PrimitiveType::Sphere ||
                             a.primitive == PrimitiveType::Plane);
    if (a.primitive != b.primitive && !primToMesh) d.add(w, std::string("tipo ") + primitiveTypeName(b.primitive));
    cmpV(d, w, "posicion", a.position, b.position, o.tol);
    if (o.rotationEquivalent) {
        const glm::mat3 ra = rotOf(a.rotationDeg), rb = rotOf(b.rotationDeg);
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                if (std::fabs(ra[c][r] - rb[c][r]) > 1e-4f) {
                    d.add(w, "orientacion distinta");
                    c = r = 3;
                }
    } else {
        cmpV(d, w, "rotacion HPB", a.rotationDeg, b.rotationDeg, o.tol);
    }
    cmpV(d, w, "escala", a.scale, b.scale, o.tol);
    if (a.editorVis != b.editorVis) d.add(w, "visibilidad en visor " + std::to_string((int)b.editorVis));
    if (a.renderVis != b.renderVis) d.add(w, "visibilidad en render " + std::to_string((int)b.renderVis));
    const std::string matA = a.materialIndex >= 0 && (size_t)a.materialIndex < ma.size() ? ma[(size_t)a.materialIndex].name : "";
    const std::string matB = b.materialIndex >= 0 && (size_t)b.materialIndex < mb.size() ? mb[(size_t)b.materialIndex].name : "";
    if (matA != matB) d.add(w, "material '" + matB + "' (esperado '" + matA + "')");
    if (a.materialIndex >= 0) {
        const TextureTagData &x = a.texTag, &y = b.texTag;
        if (x.projection != y.projection) d.add(w, "proyeccion " + std::to_string(y.projection));
        cmpF(d, w, "desplazamiento U", x.offset.x, y.offset.x, o.tol);
        cmpF(d, w, "desplazamiento V", x.offset.y, y.offset.y, o.tol);
        cmpF(d, w, "longitud U", x.length.x, y.length.x, o.tol);
        cmpF(d, w, "longitud V", x.length.y, y.length.y, o.tol);
        if (x.tile != y.tile || x.seamless != y.seamless || x.side != y.side) d.add(w, "mosaico/sin costuras/lado");
    }
    if (a.primitive == PrimitiveType::Light) {
        const LightData &x = a.light, &y = b.light;
        if (x.kind != y.kind) d.add(w, "tipo de luz " + std::to_string((int)y.kind));
        cmpV(d, w, "color luz", x.color, y.color, o.tol);
        cmpF(d, w, "intensidad", x.intensity, y.intensity, o.tol);
        cmpF(d, w, "angulo exterior", x.outerAngleDeg, y.outerAngleDeg, o.tol);
        cmpF(d, w, "angulo interior", x.innerAngleDeg, y.innerAngleDeg, o.tol);
        if (x.shadow != y.shadow) d.add(w, "sombra " + std::to_string(y.shadow));
        if (x.falloff != y.falloff) d.add(w, "decaimiento " + std::to_string(y.falloff));
        cmpF(d, w, "radio decaimiento", x.falloffRadius, y.falloffRadius, o.tol);
        cmpF(d, w, "radio interior", x.innerRadius, y.innerRadius, o.tol);
        if (x.areaShape != y.areaShape) d.add(w, "forma de area " + std::to_string(y.areaShape));
        cmpV(d, w, "tamano area", x.areaSize, y.areaSize, o.tol);
    }
    if (a.primitive == PrimitiveType::Camera) {
        const CameraData &x = a.camera, &y = b.camera;
        if (x.projection != y.projection) d.add(w, "proyeccion de camara " + std::to_string(y.projection));
        cmpF(d, w, "focal", x.focalMm, y.focalMm, o.tol);
        cmpF(d, w, "sensor", x.sensorMm, y.sensorMm, o.tol);
        cmpF(d, w, "zoom", x.zoom, y.zoom, o.tol);
        if (x.target != y.target) d.add(w, "objetivo '" + y.target + "' (esperado '" + x.target + "')");
    }
    if (a.primitive == PrimitiveType::Mesh && a.mesh && b.mesh) {
        if (o.meshExact) {
            if (a.mesh->vertices.size() != b.mesh->vertices.size() || a.mesh->indices != b.mesh->indices)
                d.add(w, "malla distinta");
            else
                for (size_t i = 0; i < a.mesh->vertices.size(); ++i) {
                    const Vertex &x = a.mesh->vertices[i], &y = b.mesh->vertices[i];
                    if (x.position != y.position || x.normal != y.normal || x.uv != y.uv) {
                        d.add(w, "vertice " + std::to_string(i) + " distinto");
                        break;
                    }
                }
        } else {
            // Un cuadrilatero de C4D no guarda diagonal: al volver puede
            // partirse por la otra. Se exige el mismo numero de triangulos y
            // el mismo conjunto de esquinas (posicion + UV).
            auto ca = corners(*a.mesh, std::max(o.tol, 1e-5f)), cb = corners(*b.mesh, std::max(o.tol, 1e-5f));
            const bool sameCount = ca.size() == cb.size();
            ca.erase(std::unique(ca.begin(), ca.end()), ca.end());
            cb.erase(std::unique(cb.begin(), cb.end()), cb.end());
            if (!sameCount || ca != cb)
                d.add(w, "esquinas de la malla (posicion + UV) distintas: " + std::to_string(cb.size()) + " (esperadas " +
                             std::to_string(ca.size()) + ")");
        }
    }
    const std::pair<const char*, std::pair<const AnimTrack*, const AnimTrack*>> tracks[] = {
        {"posicion", {&a.animPos, &b.animPos}}, {"rotacion", {&a.animRot, &b.animRot}}, {"escala", {&a.animScale, &b.animScale}}};
    for (const auto& [what, t] : tracks) {
        if (t.first->keys.size() != t.second->keys.size()) {
            d.add(w, std::string("claves de ") + what + " " + std::to_string(t.second->keys.size()) + " (esperadas " +
                         std::to_string(t.first->keys.size()) + ")");
            continue;
        }
        for (size_t k = 0; k < t.first->keys.size(); ++k) {
            if (t.first->keys[k].frame != t.second->keys[k].frame) d.add(w, std::string("fotograma de clave de ") + what);
            if (t.first->keys[k].interp != t.second->keys[k].interp) d.add(w, std::string("interpolacion de clave de ") + what);
            cmpV(d, w + " clave " + std::to_string(t.first->keys[k].frame), what, t.first->keys[k].value,
                 t.second->keys[k].value, o.tol);
        }
    }
    if (a.children().size() != b.children().size()) {
        d.add(w, "hijos " + std::to_string(b.children().size()) + " (esperados " + std::to_string(a.children().size()) + ")");
        return;
    }
    for (size_t i = 0; i < a.children().size(); ++i)
        compareNode(d, *a.children()[i], *b.children()[i], ma, mb, o, w + "/" + a.children()[i]->name());
}

// Compara las escenas (los hijos de las raices) y los materiales por nombre.
inline Diffs compareScenes(const SceneNode& a, const std::vector<Material>& ma, const SceneNode& b,
                           const std::vector<Material>& mb, const CompareOptions& o) {
    Diffs d;
    for (const Material& m : ma) {
        auto it = std::find_if(mb.begin(), mb.end(), [&](const Material& x) { return x.name == m.name; });
        if (it == mb.end()) d.add("material '" + m.name + "'", "no ha vuelto");
        else compareMaterial(d, m, *it, o);
    }
    if (a.children().size() != b.children().size()) {
        d.add("raiz", "objetos " + std::to_string(b.children().size()) + " (esperados " + std::to_string(a.children().size()) + ")");
        return d;
    }
    for (size_t i = 0; i < a.children().size(); ++i)
        compareNode(d, *a.children()[i], *b.children()[i], ma, mb, o, a.children()[i]->name());
    return d;
}

} // namespace iron3d::testio
