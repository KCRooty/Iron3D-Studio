// Importador de .c4d con el Cineware SDK de MAXON (ver CinewareImporter.h).
// Solo se compila con -DIRON3D_CINEWARE_SDK=<ruta>; el SDK no esta en el
// repositorio. Codigo propio de Iron3D escrito sobre la API publica del SDK
// (ningun ejemplo ni documentacion de MAXON copiado aqui).
//
// Que se importa:
// - Jerarquia, nombres, posicion/rotacion/escala (espejo en Z y cm ->
//   unidades, ver c4d_compat/C4DGeometry.h) y visibilidad de visor/render.
// - Objetos poligonales (con la deformacion aplicada si C4D la guardo).
// - Generadores y primitivas con la geometria que C4D guarda en el archivo
//   cuando esta activada "Guardar polígonos para Melange/Cineware"
//   (Preferencias > Archivos). Si no la hay, las primitivas basicas se
//   reconstruyen desde sus parametros y el resto llega como Nulo con aviso.
// - Luces (como Luz de Iron3D), materiales estandar (color, rugosidad y metal
//   del primer reflejo) y su asignacion por etiqueta de textura, heredada
//   por los hijos como en C4D.
// - Suavizado Phong segun la etiqueta Phong de cada objeto.

#include "CinewareImporter.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "core/Paths.h"

#include "c4d.h"
#include "default_alien_overloads.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <unordered_map>

// Cineware busca esta funcion en el espacio de nombres global. El ID se
// escribe en los .c4d que exporta Iron3D (CinewareExporter.cpp). 1000001 es
// PROVISIONAL: hay que cambiarlo por un ID registrado por Static Forge en
// developers.maxon.net antes de publicar una version que exporte .c4d.
void GetWriterInfo(cineware::Int32& id, cineware::String& appname) {
    id = 1000001; // provisional (pendiente de registrar)
    appname = "Iron3D Studio";
}

namespace iron3d {

namespace {

using namespace cineware;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultPhongRad = 80.0 * kPi / 180.0; // angulo por defecto de la etiqueta Phong

struct Ctx {
    C4DImportReport* report = nullptr;
    std::vector<iron3d::Material> materials; // nuevos, se anaden al final si todo sale bien
    int materialBase = 0;                    // indice que tendra materials[0] en la lista de la escena
    std::unordered_map<BaseMaterial*, int> materialIndex;
    std::map<std::string, int> warnings;

    void warn(const std::string& text) { ++warnings[text]; }
};

std::string utf8(const cineware::String& s) {
    Char* c = s.GetCStringCopy(STRINGENCODING_UTF8);
    std::string out = c ? c : "";
    DeleteMem(c);
    return out;
}

glm::dvec3 vec(const cineware::Vector& v) { return {v.x, v.y, v.z}; }

// Matriz de C4D como dmat4 (columnas v1, v2, v3, off), en el espacio de C4D.
glm::dmat4 mat(const cineware::Matrix& m) {
    return glm::dmat4(glm::dvec4(vec(m.v1), 0.0), glm::dvec4(vec(m.v2), 0.0), glm::dvec4(vec(m.v3), 0.0),
                      glm::dvec4(vec(m.off), 1.0));
}

VisMode visFrom(Int32 mode) {
    if (mode == MODE_ON) return VisMode::On;
    if (mode == MODE_OFF) return VisMode::Off;
    return VisMode::Default;
}

std::string typeLabel(BaseObject* op) {
    switch (op->GetType()) {
        case Osds: return "Subdivision Surface";
        case Osymmetry: return "Simetría";
        case Oextrude: return "Extrusión";
        case Olathe: return "Torno";
        case Oloft: return "Forro";
        case Osweep: return "Recorrido";
        case Oarray: return "Matriz";
        case Oboole: return "Booleano";
        case Oinstance: return "Instancia";
        case Ometaball: return "Metaball";
        case Ocamera: return "Cámara";
        case Osky: return "Cielo";
        case Ofloor: return "Suelo";
        case Oenvironment: return "Entorno";
        case Obackground: return "Fondo";
        case Oforeground: return "Primer plano";
        case Ostage: return "Escenario";
        case Ospline: return "Spline";
        case Ocapsule: return "Cápsula";
        case Otube: return "Tubo";
        default: return "tipo " + std::to_string((int)op->GetType());
    }
}

// --- Materiales -----------------------------------------------------------------

int materialFor(BaseMaterial* m, Ctx& ctx) {
    if (!m) return -1;
    auto it = ctx.materialIndex.find(m);
    if (it != ctx.materialIndex.end()) return it->second;

    iron3d::Material out;
    out.name = utf8(m->GetName());
    if (m->GetType() == Mmaterial) {
        BaseContainer* bc = m->GetDataInstance();
        if (bc->GetBool(MATERIAL_USE_COLOR, true)) {
            const cineware::Vector c = bc->GetVector(MATERIAL_COLOR_COLOR, cineware::Vector(0.8, 0.8, 0.8));
            const double br = bc->GetFloat(MATERIAL_COLOR_BRIGHTNESS, 1.0);
            out.baseColor = glm::clamp(glm::vec3((float)(c.x * br), (float)(c.y * br), (float)(c.z * br)), 0.0f, 1.0f);
        } else {
            out.baseColor = glm::vec3(0.0f);
        }
        out.roughness = 0.5f;
        out.metallic = 0.0f;
        const bool colorOn = bc->GetBool(MATERIAL_USE_COLOR, true);
        const bool debug = std::getenv("IRON3D_C4D_DEBUG") != nullptr;
        if (bc->GetBool(MATERIAL_USE_REFLECTION, true)) {
            cineware::Material* mm = static_cast<cineware::Material*>(m);
            const Int32 n = mm->GetReflectionLayerCount();
            for (Int32 i = 0; i < n; ++i) {
                ReflectionLayer* layer = mm->GetReflectionLayerIndex(i);
                if (!layer || (layer->GetFlags() & REFLECTION_FLAG_TRANSPARENCY)) continue;
                const Int32 id = layer->GetDataID();
                const Int32 dist = bc->GetInt32(id + REFLECTION_LAYER_MAIN_DISTRIBUTION, REFLECTION_DISTRIBUTION_GGX);
                const Int32 fresnel = bc->GetInt32(id + REFLECTION_LAYER_FRESNEL_MODE, REFLECTION_FRESNEL_NONE);
                const double strength = bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_REFLECTION, 1.0);
                const double rough = bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_ROUGHNESS, 0.1);
                const cineware::Vector lc = bc->GetVector(id + REFLECTION_LAYER_COLOR_COLOR, cineware::Vector(1.0, 1.0, 1.0));
                const double lb = bc->GetFloat(id + REFLECTION_LAYER_COLOR_BRIGHTNESS, 1.0);
                if (debug)
                    std::fprintf(stderr, "[c4d] material '%s' capa %d: dist=%d fresnel=%d fuerza=%.2f rugosidad=%.2f color=(%.2f %.2f %.2f)x%.2f color_on=%d\n",
                                 out.name.c_str(), (int)i, (int)dist, (int)fresnel, strength, rough, lc.x, lc.y, lc.z, lb,
                                 (int)colorOn);
                if (dist == REFLECTION_DISTRIBUTION_LAMBERTIAN || dist == REFLECTION_DISTRIBUTION_ORENNAYAR) continue;
                // Las capas "Specular" heredadas solo dan el brillo, no un reflejo de verdad.
                const bool legacySpecular = dist == REFLECTION_DISTRIBUTION_SPECULAR_BLINN ||
                                            dist == REFLECTION_DISTRIBUTION_SPECULAR_PHONG;
                out.roughness = (float)std::clamp(legacySpecular ? 0.5 : rough, 0.0, 1.0);
                const glm::vec3 layerColor =
                    glm::clamp(glm::vec3((float)(lc.x * lb), (float)(lc.y * lb), (float)(lc.z * lb)), 0.0f, 1.0f);
                if (fresnel == REFLECTION_FRESNEL_CONDUCTOR) {
                    out.metallic = 1.0f;
                    out.baseColor = layerColor;
                } else if (fresnel == REFLECTION_FRESNEL_NONE && !legacySpecular) {
                    // Sin Fresnel el reflejo es igual en todos los angulos: se
                    // comporta como un metal en la proporcion de su fuerza.
                    out.metallic = (float)std::clamp(strength, 0.0, 1.0);
                    if (!colorOn || out.metallic >= 0.5f) out.baseColor = layerColor * (float)std::clamp(strength, 0.0, 1.0);
                }
                break; // Iron3D tiene un solo reflejo: vale el primero
            }
        }
    } else {
        ctx.warn("Materiales que no son el material estándar de C4D (p. ej. de otros motores): se importan en gris");
    }
    const int index = ctx.materialBase + (int)ctx.materials.size();
    ctx.materials.push_back(std::move(out));
    ctx.materialIndex.emplace(m, index);
    ++ctx.report->materials;
    return index;
}

// Material del objeto: la etiqueta de textura mas a la derecha (la que manda en C4D).
int materialOfObject(BaseObject* op, int inherited, Ctx& ctx) {
    int result = inherited;
    for (BaseTag* t = op->GetFirstTag(); t; t = t->GetNext()) {
        if (t->GetType() != Ttexture) continue;
        if (BaseMaterial* m = static_cast<TextureTag*>(t)->GetMaterial()) result = materialFor(m, ctx);
    }
    return result;
}

// Angulo de suavizado de la etiqueta Phong; -1 = sin etiqueta (caras planas).
double phongAngle(BaseObject* op) {
    BaseTag* t = op->GetTag(Tphong);
    if (!t) return -1.0;
    BaseContainer* bc = t->GetDataInstance();
    if (!bc->GetBool(PHONGTAG_PHONG_ANGLELIMIT, false)) return kPi;
    return bc->GetFloat(PHONGTAG_PHONG_ANGLE, kDefaultPhongRad);
}

// --- Geometria ------------------------------------------------------------------

void appendPolygons(PolygonObject* po, const glm::dmat4& xform, c4d::PolySoup& out) {
    const cineware::Vector* p = po->GetPointR();
    const CPolygon* f = po->GetPolygonR();
    const Int32 np = po->GetPointCount(), nf = po->GetPolygonCount();
    if (!p || !f || np <= 0 || nf <= 0) return;
    c4d::PolySoup s;
    s.points.reserve((size_t)np);
    for (Int32 i = 0; i < np; ++i) s.points.push_back(vec(p[i]));
    s.polys.reserve((size_t)nf);
    for (Int32 i = 0; i < nf; ++i) s.polys.push_back({(int)f[i].a, (int)f[i].b, (int)f[i].c, (int)f[i].d});
    out.append(s, xform);
}

// Objeto de una cache (jerarquia virtual que guarda C4D): primero su cache
// de deformacion, si no su propia cache, si no el objeto mismo; y despues
// sus hijos. Es el recorrido que describe la documentacion de GetCache().
void collectVirtual(BaseObject* op, const glm::dmat4& xform, c4d::PolySoup& out, int depth) {
    if (!op || depth > 64) return;
    if (BaseObject* dc = op->GetDeformCache()) {
        collectVirtual(dc, xform, out, depth + 1);
    } else if (BaseObject* c = op->GetCache()) {
        for (BaseObject* k = c; k; k = k->GetNext()) collectVirtual(k, xform * mat(k->GetMl()), out, depth + 1);
    } else if (!op->GetBit(BIT_CONTROLOBJECT) && op->GetType() == Opolygon) {
        appendPolygons(static_cast<PolygonObject*>(op), xform, out);
    }
    for (BaseObject* ch = op->GetDown(); ch; ch = ch->GetNext())
        collectVirtual(ch, xform * mat(ch->GetMl()), out, depth + 1);
}

// Geometria guardada por C4D para un generador/primitiva (en su espacio local).
bool geometryFromCache(BaseObject* op, c4d::PolySoup& out) {
    BaseObject* c = op->GetDeformCache();
    if (c) {
        collectVirtual(c, glm::dmat4(1.0), out, 0);
    } else if ((c = op->GetCache()) != nullptr) {
        for (BaseObject* k = c; k; k = k->GetNext()) collectVirtual(k, mat(k->GetMl()), out, 0);
    }
    return !out.empty();
}

bool rebuildPrimitive(BaseObject* op, c4d::PolySoup& out, Ctx& ctx) {
    BaseContainer* bc = op->GetDataInstance();
    const c4d::Axis axis = c4d::axisFromC4D(bc->GetInt32(PRIM_AXIS, 2));
    switch (op->GetType()) {
        case Ocube: {
            const cineware::Vector v = bc->GetVector(PRIM_CUBE_LEN, cineware::Vector(200.0, 200.0, 200.0));
            if (bc->GetBool(PRIM_CUBE_DOFILLET, false)) ctx.warn("Cubos con canto redondeado: se importan sin el redondeo");
            out = c4d::makeCube(vec(v));
            return true;
        }
        case Osphere:
            out = c4d::makeSphere(bc->GetFloat(PRIM_SPHERE_RAD, 100.0), bc->GetInt32(PRIM_SPHERE_SUB, 24));
            return true;
        case Oplane:
            out = c4d::makePlane(bc->GetFloat(PRIM_PLANE_WIDTH, 400.0), bc->GetFloat(PRIM_PLANE_HEIGHT, 400.0), axis);
            return true;
        case Ocylinder:
            if (bc->GetBool(PRIM_CYLINDER_FILLET, false)) ctx.warn("Cilindros con canto redondeado: se importan sin el redondeo");
            out = c4d::makeCylinder(bc->GetFloat(PRIM_CYLINDER_RADIUS, 50.0), bc->GetFloat(PRIM_CYLINDER_HEIGHT, 200.0),
                                    bc->GetInt32(PRIM_CYLINDER_SEG, 36), bc->GetBool(PRIM_CYLINDER_CAPS, true), axis);
            return true;
        case Ocone:
            if (bc->GetBool(PRIM_CONE_TOPFILLET, false) || bc->GetBool(PRIM_CONE_BOTFILLET, false))
                ctx.warn("Conos con canto redondeado: se importan sin el redondeo");
            out = c4d::makeCone(bc->GetFloat(PRIM_CONE_TRAD, 0.0), bc->GetFloat(PRIM_CONE_BRAD, 100.0),
                                bc->GetFloat(PRIM_CONE_HEIGHT, 200.0), bc->GetInt32(PRIM_CONE_SEG, 36),
                                bc->GetBool(PRIM_CONE_CAPS, true), axis);
            return true;
        case Otorus:
            out = c4d::makeTorus(bc->GetFloat(PRIM_TORUS_OUTERRAD, 200.0), bc->GetFloat(PRIM_TORUS_INNERRAD, 50.0),
                                 bc->GetInt32(PRIM_TORUS_SEG, 36), bc->GetInt32(PRIM_TORUS_CSUB, 18), axis);
            return true;
        case Odisc:
            out = c4d::makeDisc(bc->GetFloat(PRIM_DISC_IRAD, 0.0), bc->GetFloat(PRIM_DISC_ORAD, 100.0),
                                bc->GetInt32(PRIM_DISC_SEG, 36), axis);
            return true;
        case Opyramid: {
            const cineware::Vector v = bc->GetVector(PRIM_PYRAMID_LEN, cineware::Vector(200.0, 200.0, 200.0));
            out = c4d::makePyramid(vec(v));
            return true;
        }
        case Osinglepoly:
            if (bc->GetBool(PRIM_POLY_TRIANG, false)) ctx.warn("Primitiva Polígono en modo triángulo: se importa como cuadrilátero");
            out = c4d::makePlane(bc->GetFloat(PRIM_POLY_WIDTH, 200.0), bc->GetFloat(PRIM_POLY_HEIGHT, 200.0), axis);
            return true;
        default:
            return false;
    }
}

// Caras planas por defecto para lo que en C4D son cajas; suave para el resto.
double defaultPrimitivePhong(Int32 type) {
    return (type == Ocube || type == Opyramid) ? -1.0 : kDefaultPhongRad;
}

void importObject(BaseObject* op, SceneNode& parent, int inheritedMaterial, bool isGeneratorInput, Ctx& ctx) {
    C4DImportReport& rep = *ctx.report;
    const std::string name = utf8(op->GetName());
    SceneNode* node = parent.addChild(name.empty() ? std::string("Objeto") : name);
    ++rep.objects;

    const cineware::Matrix& ml = op->GetMl();
    const c4d::TRS trs = c4d::toIronTRS(vec(ml.off), vec(ml.v1), vec(ml.v2), vec(ml.v3));
    node->position = trs.position;
    node->rotationDeg = trs.rotationDeg;
    node->scale = trs.scale;
    node->editorVis = visFrom(op->GetEditorMode());
    node->renderVis = visFrom(op->GetRenderMode());
    if (isGeneratorInput) {
        // Objeto de entrada de un generador importado con su geometria final:
        // en C4D no se ve por separado, asi que aqui tampoco.
        node->editorVis = node->renderVis = VisMode::Off;
    }
    const int material = materialOfObject(op, inheritedMaterial, ctx);
    node->materialIndex = material;

    const Int32 type = op->GetType();
    c4d::PolySoup soup;
    double phong = phongAngle(op);
    bool childrenAreInputs = false;

    if (type == Opolygon) {
        BaseObject* dc = op->GetDeformCache();
        PolygonObject* po = (dc && dc->GetType() == Opolygon) ? static_cast<PolygonObject*>(dc)
                                                               : static_cast<PolygonObject*>(op);
        appendPolygons(po, glm::dmat4(1.0), soup);
    } else if (type == Olight) {
        node->primitive = PrimitiveType::Light;
        ++rep.lights;
    } else if (type == Onull) {
        // grupo, nada mas
    } else if (geometryFromCache(op, soup)) {
        ++rep.generatorsFromCache;
        childrenAreInputs = op->GetDown() != nullptr;
    } else if (rebuildPrimitive(op, soup, ctx)) {
        ++rep.primitivesRebuilt;
        if (phong < 0.0) phong = defaultPrimitivePhong(type);
    } else if (type == Ocamera) {
        ctx.warn("Cámaras: Iron3D todavía no tiene objetos cámara; se importan como Nulo (conservan posición)");
    } else if (type == Ospline || type == Osplineprimitive || (type >= Osplineprofile && type <= Osplinecontour)) {
        ctx.warn("Splines: Iron3D todavía no tiene splines; se importan como Nulo (conservan posición)");
    } else if (type == Osky || type == Ofloor || type == Oenvironment || type == Obackground ||
               type == Oforeground || type == Ostage) {
        ctx.warn("Objetos de escena (" + typeLabel(op) + "...): Iron3D todavía no los tiene; se importan como Nulo");
    } else if (type == Ojoint) {
        ctx.warn("Articulaciones (rigging): Iron3D todavía no tiene esqueletos; se importan como Nulo (conservan posición)");
    } else if (type == Oskin || op->IsInstanceOf(Obasedeform)) {
        ctx.warn("Deformadores (también Piel): su efecto solo llega si C4D guardó la malla deformada; el deformador se "
                 "importa como Nulo");
    } else {
        ctx.warn(typeLabel(op) + ": sin geometría guardada en el archivo, se importa como Nulo "
                 "(guarda el .c4d con \"Guardar polígonos para Melange\" activado para traerla)");
    }

    if (!soup.empty()) {
        auto mesh = std::make_shared<MeshData>(c4d::toIronMesh(soup, phong));
        rep.points += (long long)soup.points.size();
        rep.polygons += (long long)soup.polys.size();
        if (!mesh->indices.empty()) {
            node->primitive = PrimitiveType::Mesh;
            node->mesh = std::move(mesh);
            ++rep.meshes;
        }
    }

    for (BaseObject* ch = op->GetDown(); ch; ch = ch->GetNext())
        importObject(ch, *node, material, isGeneratorInput || childrenAreInputs, ctx);
}

} // namespace

bool cinewareAvailable() { return true; }

bool isC4DPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".c4d";
}

bool importC4D(const std::filesystem::path& file, SceneNode& parent, std::vector<iron3d::Material>& materials,
               C4DImportReport& report, std::string* outError) {
    report = C4DImportReport{};
    report.fileName = pathUtf8(file.filename());

    // Ruta en UTF-16: con el code page local fallan las rutas con acentos o ñ.
    const std::u16string wide = file.u16string();
    Filename fn{cineware::String(reinterpret_cast<const UInt16*>(wide.c_str()))};
    BaseDocument* doc = LoadDocument(fn, SCENEFILTER_OBJECTS | SCENEFILTER_MATERIALS);
    if (!doc) {
        if (outError) *outError = "Cineware no ha podido leer el archivo (¿no es un .c4d o está dañado?)";
        return false;
    }

    Ctx ctx;
    ctx.report = &report;
    ctx.materialBase = (int)materials.size();
    SceneNode temp("__c4d_import_tmp__");
    for (BaseObject* op = doc->GetFirstObject(); op; op = op->GetNext()) importObject(op, temp, -1, false, ctx);
    BaseDocument::Free(doc);

    for (const auto& w : ctx.warnings)
        report.warnings.push_back(w.second > 1 ? w.first + " (" + std::to_string(w.second) + ")" : w.first);
    for (iron3d::Material& m : ctx.materials) materials.push_back(std::move(m));
    parent.appendChildrenFrom(temp);
    return true;
}

} // namespace iron3d
