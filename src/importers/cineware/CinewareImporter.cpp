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
// - Materiales estandar: Color, Luminancia, Transparencia (con IOR), Normal,
//   Relieve, Alfa y todas las capas de Reflectancia tal cual; las texturas de
//   imagen de cada canal (ruta como esta en el archivo, resuelta junto al
//   .c4d o en tex/). Asignados por etiqueta de textura (con su proyeccion,
//   desplazamiento y longitud), heredada por los hijos como en C4D.
// - UV de la etiqueta UVW.
// - Luces (tipo, color, intensidad, angulos, sombra, decaimiento, area) y
//   Camaras (proyeccion, focal, sensor, zoom y etiqueta Objetivo).
// - Posicion/Rotacion/Escala con los numeros HPB exactos de C4D (sin
//   transformacion congelada) y sus pistas de animacion.
// - Suavizado Phong segun la etiqueta Phong de cada objeto.

#include "CinewareImporter.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "core/Paths.h"
#include "core/Material.h"

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
    std::string docDir;                      // carpeta del .c4d (texturas relativas)
    Float fps = 30.0;

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

// Ruta de la imagen de un canal. El enlace apunta a un shader: si es Imagen
// (Xbitmap) se toma su archivo; si es otro (Capa, Filtro, Fusion...), la
// primera Imagen que tenga dentro.
std::string bitmapPath(BaseList2D* link, Ctx& ctx, int depth = 0) {
    if (!link || depth > 8) return {};
    BaseShader* sh = static_cast<BaseShader*>(link);
    if (sh->GetType() == Xbitmap) {
        GeData d;
        if (!sh->GetParameter(BITMAPSHADER_FILENAME, d)) return {};
        std::string p = utf8(d.GetFilename().GetString());
        // Cinema 4D guarda las rutas relativas como "./tex/a.png": se quita
        // el "./" para que la ruta vuelva igual que se escribio.
        while (p.size() > 2 && p[0] == '.' && (p[1] == '/' || p[1] == (char)92)) p.erase(0, 2);
        return p;
    }
    for (BaseShader* k = sh->GetDown(); k; k = k->GetNext()) {
        const std::string inner = bitmapPath(k, ctx, depth + 1);
        if (!inner.empty()) {
            if (depth == 0) ctx.warn("Canales con shaders de C4D (Capa, Filtro, Fusión...): se usa la imagen que llevan dentro");
            return inner;
        }
    }
    if (depth == 0) ctx.warn("Canales con shaders procedurales de C4D (Ruido, Degradado...): Iron3D no los tiene, se ignoran");
    return {};
}

glm::vec3 colorTimes(BaseContainer* bc, Int32 colorId, Int32 brightId, const cineware::Vector& defColor) {
    const cineware::Vector c = bc->GetVector(colorId, defColor);
    const double br = bc->GetFloat(brightId, 1.0);
    return glm::vec3((float)(c.x * br), (float)(c.y * br), (float)(c.z * br));
}

void noteTexture(const std::string& path, Ctx& ctx) {
    if (path.empty()) return;
    ++ctx.report->textures;
    if (iron3d::resolveTexturePath(path, ctx.docDir).empty()) {
        ++ctx.report->texturesMissing;
        ctx.warn("Texturas que no están en disco (ni junto al .c4d ni en tex/): se conserva la ruta");
    }
}

int materialFor(BaseMaterial* m, Ctx& ctx) {
    if (!m) return -1;
    auto it = ctx.materialIndex.find(m);
    if (it != ctx.materialIndex.end()) return it->second;

    iron3d::Material out;
    out.name = utf8(m->GetName());
    out.texBaseDir = ctx.docDir;
    out.roughness = 0.5f;
    out.metallic = 0.0f;
    if (m->GetType() == Mmaterial) {
        BaseContainer* bc = m->GetDataInstance();
        auto tex = [&](iron3d::TexSlot slot, Int32 linkId) {
            std::string& t = out.texture(slot);
            t = bitmapPath(bc->GetLink(linkId), ctx);
            noteTexture(t, ctx);
        };
        const bool colorOn = bc->GetBool(MATERIAL_USE_COLOR, true);
        if (colorOn) {
            out.baseColor = glm::clamp(colorTimes(bc, MATERIAL_COLOR_COLOR, MATERIAL_COLOR_BRIGHTNESS, cineware::Vector(0.8, 0.8, 0.8)),
                                       0.0f, 1.0f);
            tex(iron3d::TexSlot::Color, MATERIAL_COLOR_SHADER);
        } else {
            out.baseColor = glm::vec3(0.0f);
        }
        if (bc->GetBool(MATERIAL_USE_LUMINANCE, false)) {
            out.emission = colorTimes(bc, MATERIAL_LUMINANCE_COLOR, MATERIAL_LUMINANCE_BRIGHTNESS, cineware::Vector(1.0, 1.0, 1.0));
            tex(iron3d::TexSlot::Emission, MATERIAL_LUMINANCE_SHADER);
        }
        if (bc->GetBool(MATERIAL_USE_TRANSPARENCY, false)) {
            out.transparency = colorTimes(bc, MATERIAL_TRANSPARENCY_COLOR, MATERIAL_TRANSPARENCY_BRIGHTNESS,
                                          cineware::Vector(1.0, 1.0, 1.0));
            out.ior = (float)bc->GetFloat(MATERIAL_TRANSPARENCY_REFRACTION, 1.0);
            tex(iron3d::TexSlot::Transparency, MATERIAL_TRANSPARENCY_SHADER);
        }
        if (bc->GetBool(MATERIAL_USE_NORMAL, false)) {
            out.normalStrength = (float)bc->GetFloat(MATERIAL_NORMAL_STRENGTH, 1.0);
            tex(iron3d::TexSlot::Normal, MATERIAL_NORMAL_SHADER);
        }
        if (bc->GetBool(MATERIAL_USE_BUMP, false)) {
            out.bumpStrength = (float)bc->GetFloat(MATERIAL_BUMP_STRENGTH, 0.2);
            tex(iron3d::TexSlot::Bump, MATERIAL_BUMP_SHADER);
        }
        if (bc->GetBool(MATERIAL_USE_ALPHA, false)) tex(iron3d::TexSlot::Alpha, MATERIAL_ALPHA_SHADER);
        if (bc->GetBool(MATERIAL_USE_REFLECTION, true)) {
            cineware::Material* mm = static_cast<cineware::Material*>(m);
            const Int32 n = mm->GetReflectionLayerCount();
            for (Int32 i = 0; i < n; ++i) {
                ReflectionLayer* layer = mm->GetReflectionLayerIndex(i);
                if (!layer) continue;
                const Int32 id = layer->GetDataID();
                iron3d::ReflectLayer l;
                l.name = utf8(layer->GetName());
                l.transparencyLayer = (layer->GetFlags() & REFLECTION_FLAG_TRANSPARENCY) != 0;
                l.distribution = bc->GetInt32(id + REFLECTION_LAYER_MAIN_DISTRIBUTION, REFLECTION_DISTRIBUTION_GGX);
                l.fresnel = bc->GetInt32(id + REFLECTION_LAYER_FRESNEL_MODE, REFLECTION_FRESNEL_NONE);
                l.fresnelPreset = bc->GetInt32(id + REFLECTION_LAYER_FRESNEL_PRESET, 0);
                l.strength = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_REFLECTION, 1.0);
                l.specular = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_SPECULAR, 0.2);
                l.roughness = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_ROUGHNESS, 0.1);
                l.ior = (float)bc->GetFloat(id + REFLECTION_LAYER_FRESNEL_VALUE_IOR, 1.5);
                l.color = colorTimes(bc, id + REFLECTION_LAYER_COLOR_COLOR, id + REFLECTION_LAYER_COLOR_BRIGHTNESS,
                                     cineware::Vector(1.0, 1.0, 1.0));
                l.roughnessTex = bitmapPath(bc->GetLink(id + REFLECTION_LAYER_MAIN_SHADER_ROUGHNESS), ctx);
                l.colorTex = bitmapPath(bc->GetLink(id + REFLECTION_LAYER_COLOR_TEXTURE), ctx);
                noteTexture(l.roughnessTex, ctx);
                noteTexture(l.colorTex, ctx);
                out.layers.push_back(std::move(l));
            }
            if (!iron3d::pbrFromLayers(out.layers, colorOn, out.baseColor, out.metallic, out.roughness)) {
                out.metallic = 0.0f;
                out.roughness = 0.5f;
            }
            for (const iron3d::ReflectLayer& l : out.layers)
                if (!l.transparencyLayer && l.distribution != REFLECTION_DISTRIBUTION_LAMBERTIAN &&
                    l.distribution != REFLECTION_DISTRIBUTION_ORENNAYAR) {
                    out.texture(iron3d::TexSlot::Roughness) = l.roughnessTex;
                    break;
                }
            if (std::getenv("IRON3D_C4D_DEBUG"))
                std::fprintf(stderr, "[c4d] material '%s': %d capas -> metal=%.3f rugosidad=%.3f\n", out.name.c_str(),
                             (int)out.layers.size(), out.metallic, out.roughness);
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

// Material del objeto: la etiqueta de textura mas a la derecha (la que manda
// en C4D), con su proyeccion y mosaico. Sin etiqueta se hereda del padre.
int materialOfObject(BaseObject* op, int inherited, TextureTagData& texTag, Ctx& ctx) {
    int result = inherited;
    int tags = 0;
    for (BaseTag* t = op->GetFirstTag(); t; t = t->GetNext()) {
        if (t->GetType() != Ttexture) continue;
        BaseMaterial* m = static_cast<TextureTag*>(t)->GetMaterial();
        if (!m) continue;
        ++tags;
        result = materialFor(m, ctx);
        BaseContainer* bc = t->GetDataInstance();
        TextureTagData d;
        d.projection = bc->GetInt32(TEXTURETAG_PROJECTION, TEXTURETAG_PROJECTION_UVW);
        d.offset = glm::vec2((float)bc->GetFloat(TEXTURETAG_OFFSETX, 0.0), (float)bc->GetFloat(TEXTURETAG_OFFSETY, 0.0));
        d.length = glm::vec2((float)bc->GetFloat(TEXTURETAG_LENGTHX, 1.0), (float)bc->GetFloat(TEXTURETAG_LENGTHY, 1.0));
        d.tile = bc->GetBool(TEXTURETAG_TILE, true);
        d.seamless = bc->GetBool(TEXTURETAG_SEAMLESS, false);
        d.side = bc->GetInt32(TEXTURETAG_SIDE, 0);
        texTag = d;
        if (d.projection != TEXTURETAG_PROJECTION_UVW)
            ctx.warn("Etiquetas de textura con proyección distinta de UVW (Esférica, Cúbica...): se conserva, pero Iron3D "
                     "dibuja con las UV");
        if (bc->GetString(TEXTURETAG_RESTRICTION).GetLength() > 0)
            ctx.warn("Etiquetas de textura restringidas a una selección: se aplican a todo el objeto");
    }
    if (tags > 1) ctx.warn("Objetos con varias etiquetas de textura: vale la de más a la derecha");
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
    // UV de la etiqueta UVW (una por poligono, esquinas a b c d).
    if (BaseTag* t = po->GetTag(Tuvw)) {
        UVWTag* uvw = static_cast<UVWTag*>(t);
        if (uvw->GetDataCount() == nf) {
            ConstUVWHandle h = uvw->GetDataAddressR();
            s.uvs.resize((size_t)nf);
            for (Int32 i = 0; i < nf; ++i) {
                UVWStruct u;
                UVWTag::Get(h, i, u);
                s.uvs[(size_t)i] = {glm::dvec2(u.a.x, u.a.y), glm::dvec2(u.b.x, u.b.y), glm::dvec2(u.c.x, u.c.y),
                                    glm::dvec2(u.d.x, u.d.y)};
            }
        }
    }
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

// Datos de una Luz de C4D (distancias en cm -> unidades de Iron3D).
LightData lightFrom(BaseObject* op, Ctx& ctx) {
    BaseContainer* bc = op->GetDataInstance();
    LightData l;
    const Int32 type = bc->GetInt32(LIGHT_TYPE, LIGHT_TYPE_OMNI);
    l.kind = (LightKind)std::clamp((int)type, 0, 9);
    const cineware::Vector c = bc->GetVector(LIGHT_COLOR, cineware::Vector(1.0, 1.0, 1.0));
    l.color = glm::vec3((float)c.x, (float)c.y, (float)c.z);
    l.intensity = (float)bc->GetFloat(LIGHT_BRIGHTNESS, 1.0);
    const double toDeg = 180.0 / kPi;
    l.outerAngleDeg = (float)(bc->GetFloat(LIGHT_DETAILS_OUTERANGLE, 30.0 / toDeg) * toDeg);
    l.innerAngleDeg = (float)(bc->GetFloat(LIGHT_DETAILS_INNERANGLE, 0.0) * toDeg);
    l.shadow = bc->GetInt32(LIGHT_SHADOWTYPE, LIGHT_SHADOWTYPE_NONE);
    l.falloff = bc->GetInt32(LIGHT_DETAILS_FALLOFF, LIGHT_DETAILS_FALLOFF_NONE);
    l.falloffRadius = (float)(bc->GetFloat(LIGHT_DETAILS_OUTERDISTANCE, 500.0) / c4d::kCmPerUnit);
    l.innerRadius = (float)(bc->GetFloat(LIGHT_DETAILS_INNERDISTANCE, 0.0) / c4d::kCmPerUnit);
    l.areaShape = bc->GetInt32(LIGHT_AREADETAILS_SHAPE, LIGHT_AREADETAILS_SHAPE_RECTANGLE);
    l.areaSize = glm::vec3((float)(bc->GetFloat(LIGHT_AREADETAILS_SIZEX, 200.0) / c4d::kCmPerUnit),
                           (float)(bc->GetFloat(LIGHT_AREADETAILS_SIZEY, 200.0) / c4d::kCmPerUnit),
                           (float)(bc->GetFloat(LIGHT_AREADETAILS_SIZEZ, 200.0) / c4d::kCmPerUnit));
    if (op->GetTag(Tsunexpression)) ctx.warn("Luces con expresión Sol: se importan como su luz (sin la expresión)");
    return l;
}

CameraData cameraFrom(BaseObject* op) {
    BaseContainer* bc = op->GetDataInstance();
    CameraData c;
    c.projection = bc->GetInt32(CAMERA_PROJECTION, Pperspective);
    c.focalMm = (float)bc->GetFloat(CAMERA_FOCUS, 36.0);
    c.sensorMm = (float)bc->GetFloat(CAMERAOBJECT_APERTURE, 36.0);
    c.zoom = (float)bc->GetFloat(CAMERA_ZOOM, 1.0);
    return c;
}

// Ruta por nombres desde la raiz del documento ("Grupo/Cubo").
std::string namePath(BaseObject* op) {
    std::string path;
    for (BaseObject* o = op; o; o = o->GetUp()) path = utf8(o->GetName()) + (path.empty() ? "" : "/" + path);
    return path;
}

// Pistas de Posicion/Rotacion/Escala -> pistas de Iron3D. C4D guarda una
// curva por componente; Iron3D una clave con los tres valores por fotograma:
// en los fotogramas donde solo hay clave en alguna componente, las otras se
// evaluan en su curva (o se toma el valor fijo si no tienen).
void importTracks(BaseObject* op, SceneNode& node, Ctx& ctx) {
    CCurve* curves[3][3] = {};
    bool any = false;
    for (CTrack* tr = op->GetFirstCTrack(); tr; tr = tr->GetNext()) {
        const DescID& id = tr->GetDescriptionID();
        if (id.GetDepth() < 2) {
            ctx.warn("Pistas de animación de otros parámetros (no Posición/Rotación/Escala): se ignoran");
            continue;
        }
        const Int32 a = id[0].id, b = id[1].id - VECTOR_X;
        const int ch = a == ID_BASEOBJECT_REL_POSITION ? 0 : a == ID_BASEOBJECT_REL_ROTATION ? 1 : a == ID_BASEOBJECT_REL_SCALE ? 2 : -1;
        if (ch < 0 || b < 0 || b > 2) {
            ctx.warn("Pistas de animación de otros parámetros (no Posición/Rotación/Escala): se ignoran");
            continue;
        }
        CCurve* cv = tr->GetCurve(CCURVE_CURVE, false);
        if (!cv || cv->GetKeyCount() == 0) continue;
        curves[ch][b] = cv;
        any = true;
        for (Int32 k = 0; k < cv->GetKeyCount(); ++k) {
            const CKey* key = cv->GetKey(k);
            if (key->GetInterpolation() != CINTERPOLATION_SPLINE)
                ctx.warn("Claves lineales o escalonadas: Iron3D interpola siempre en spline (los valores de las claves "
                         "llegan exactos)");
        }
    }
    if (!any) return;
    ++ctx.report->animated;
    const cineware::Vector rel[3] = {op->GetRelPos(), op->GetRelRot(), op->GetRelScale()};
    AnimTrack* tracks[3] = {&node.animPos, &node.animRot, &node.animScale};
    for (int ch = 0; ch < 3; ++ch) {
        std::vector<int> frames;
        for (int b = 0; b < 3; ++b)
            if (CCurve* cv = curves[ch][b])
                for (Int32 k = 0; k < cv->GetKeyCount(); ++k) frames.push_back(cv->GetKey(k)->GetTime().GetFrame(ctx.fps));
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        for (int f : frames) {
            double v[3] = {rel[ch].x, rel[ch].y, rel[ch].z};
            for (int b = 0; b < 3; ++b)
                if (CCurve* cv = curves[ch][b]) v[b] = cv->GetValue(BaseTime((Float)f, ctx.fps));
            glm::vec3 iron;
            if (ch == 0) iron = glm::vec3((float)(v[0] / c4d::kCmPerUnit), (float)(v[1] / c4d::kCmPerUnit), (float)(-v[2] / c4d::kCmPerUnit));
            else if (ch == 1) iron = glm::vec3((float)(v[1] * 180.0 / kPi), (float)(v[0] * 180.0 / kPi), (float)(v[2] * 180.0 / kPi));
            else iron = glm::vec3((float)v[0], (float)v[1], (float)v[2]);
            tracks[ch]->set(f, iron);
        }
    }
}

struct PendingTarget {
    SceneNode* camera;
    BaseObject* target;
};

void importObject(BaseObject* op, SceneNode& parent, int inheritedMaterial, const TextureTagData& inheritedTag,
                  bool isGeneratorInput, Ctx& ctx, std::vector<PendingTarget>& targets) {
    C4DImportReport& rep = *ctx.report;
    const std::string name = utf8(op->GetName());
    SceneNode* node = parent.addChild(name.empty() ? std::string("Objeto") : name);
    ++rep.objects;

    // Transformacion: los numeros de C4D tal cual (HPB incluido) si no hay
    // transformacion congelada; si la hay, desde la matriz.
    const cineware::Vector fp = op->GetFrozenPos(), fr = op->GetFrozenRot(), fs = op->GetFrozenScale();
    const bool frozen = fp != cineware::Vector(0.0) || fr != cineware::Vector(0.0) || fs != cineware::Vector(1.0);
    if (!frozen) {
        const cineware::Vector rp = op->GetRelPos(), rr = op->GetRelRot(), rs = op->GetRelScale();
        node->position = glm::vec3((float)(rp.x / c4d::kCmPerUnit), (float)(rp.y / c4d::kCmPerUnit), (float)(-rp.z / c4d::kCmPerUnit));
        node->rotationDeg = glm::vec3((float)(rr.y * 180.0 / kPi), (float)(rr.x * 180.0 / kPi), (float)(rr.z * 180.0 / kPi));
        node->scale = glm::vec3((float)rs.x, (float)rs.y, (float)rs.z);
    } else {
        const cineware::Matrix& ml = op->GetMl();
        const c4d::TRS trs = c4d::toIronTRS(vec(ml.off), vec(ml.v1), vec(ml.v2), vec(ml.v3));
        node->position = trs.position;
        node->rotationDeg = trs.rotationDeg;
        node->scale = trs.scale;
        ctx.warn("Objetos con transformación congelada: se importa la transformación final (sin la congelada aparte)");
    }
    node->editorVis = visFrom(op->GetEditorMode());
    node->renderVis = visFrom(op->GetRenderMode());
    if (isGeneratorInput) {
        // Objeto de entrada de un generador importado con su geometria final:
        // en C4D no se ve por separado, asi que aqui tampoco.
        node->editorVis = node->renderVis = VisMode::Off;
    }
    TextureTagData texTag = inheritedTag;
    const int material = materialOfObject(op, inheritedMaterial, texTag, ctx);
    node->materialIndex = material;
    node->texTag = texTag;
    importTracks(op, *node, ctx);

    const Int32 type = op->GetType();
    c4d::PolySoup soup;
    double phong = phongAngle(op);
    bool childrenAreInputs = false;

    if (type == Opolygon) {
        BaseObject* dc = op->GetDeformCache();
        PolygonObject* po = (dc && dc->GetType() == Opolygon) ? static_cast<PolygonObject*>(dc)
                                                               : static_cast<PolygonObject*>(op);
        appendPolygons(po, glm::dmat4(1.0), soup);
        // La cache de deformacion no siempre lleva la etiqueta UVW: la del objeto vale.
        if (soup.uvs.empty() && po != op) {
            c4d::PolySoup own;
            appendPolygons(static_cast<PolygonObject*>(op), glm::dmat4(1.0), own);
            if (own.polys.size() == soup.polys.size()) soup.uvs = own.uvs;
        }
    } else if (type == Olight) {
        node->primitive = PrimitiveType::Light;
        node->light = lightFrom(op, ctx);
        ++rep.lights;
    } else if (type == Ocamera) {
        node->primitive = PrimitiveType::Camera;
        node->camera = cameraFrom(op);
        if (BaseTag* t = op->GetTag(Ttargetexpression))
            if (BaseList2D* link = t->GetDataInstance()->GetLink(TARGETEXPRESSIONTAG_LINK))
                targets.push_back({node, static_cast<BaseObject*>(link)});
        if (node->camera.projection > Pparallel)
            ctx.warn("Cámaras con proyección especial (Izquierda, Isométrica...): se conserva, el visor la trata como paralela");
        ++rep.cameras;
    } else if (type == Onull) {
        // grupo, nada mas
    } else if (geometryFromCache(op, soup)) {
        ++rep.generatorsFromCache;
        childrenAreInputs = op->GetDown() != nullptr;
    } else if (rebuildPrimitive(op, soup, ctx)) {
        ++rep.primitivesRebuilt;
        if (phong < 0.0) phong = defaultPrimitivePhong(type);
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
        importObject(ch, *node, material, texTag, isGeneratorInput || childrenAreInputs, ctx, targets);
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
    ctx.docDir = pathUtf8(file.parent_path());
    ctx.fps = doc->GetFps() > 0 ? (Float)doc->GetFps() : 30.0;
    SceneNode temp("__c4d_import_tmp__");
    std::vector<PendingTarget> targets;
    for (BaseObject* op = doc->GetFirstObject(); op; op = op->GetNext())
        importObject(op, temp, -1, TextureTagData{}, false, ctx, targets);
    // Etiqueta Objetivo: el objetivo se guarda por su ruta de nombres.
    for (const PendingTarget& t : targets) t.camera->camera.target = namePath(t.target);
    BaseDocument::Free(doc);

    for (const auto& w : ctx.warnings)
        report.warnings.push_back(w.second > 1 ? w.first + " (" + std::to_string(w.second) + ")" : w.first);
    for (iron3d::Material& m : ctx.materials) materials.push_back(std::move(m));
    parent.appendChildrenFrom(temp);
    return true;
}

} // namespace iron3d
