// Exportador a .c4d con el Cineware SDK de MAXON (ver CinewareImporter.h).
// Solo se compila con -DIRON3D_CINEWARE_SDK=<ruta>; el SDK no esta en el
// repositorio. Codigo propio de Iron3D escrito sobre la API publica del SDK.
//
// Que se exporta (lo que Iron3D tiene hoy):
// - Jerarquia, nombres, visibilidad de visor/render y Posicion/Rotacion/
//   Escala con los numeros HPB exactos; sus pistas de animacion como pistas
//   de C4D (spline, tangentes automaticas recortadas, como las de Iron3D).
// - Cubo, Esfera y Plano como las primitivas parametricas de C4D (siguen
//   siendo editables alli); Luz y Camara con todos sus datos; Nulo como Nulo.
// - Mallas como Objeto Poligonal (vertices unidos y cuadrilateros donde se
//   puede), con etiqueta UVW si tienen UV y etiqueta Phong si estaban suavizadas.
// - Materiales como Material estandar: Color, Luminancia, Transparencia,
//   Normal, Relieve, Alfa con sus texturas (shader Imagen), y Reflectancia:
//   las capas originales tal cual si los campos PBR no se han tocado; si no,
//   una capa GGX con la rugosidad (metal = Fresnel de conductor). Asignados
//   con etiqueta de textura (proyeccion y mosaico incluidos), solo donde
//   cambian respecto al padre (C4D los hereda).
//
// El ID de aplicacion que se escribe en el archivo es el de GetWriterInfo()
// (CinewareImporter.cpp).

#include "CinewareImporter.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "core/Paths.h"
#include "core/Material.h"

#include "c4d.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

namespace iron3d {

namespace {

using namespace cineware;

constexpr double kPi = 3.14159265358979323846;

cineware::Vector cv(const glm::dvec3& v) { return cineware::Vector(v.x, v.y, v.z); }
cineware::Vector cv(const glm::vec3& v) { return cineware::Vector(v.x, v.y, v.z); }

Int32 modeOf(VisMode v) {
    if (v == VisMode::On) return MODE_ON;
    if (v == VisMode::Off) return MODE_OFF;
    return MODE_UNDEF;
}

struct Ctx {
    BaseDocument* doc = nullptr;
    const std::vector<iron3d::Material>* materials = nullptr;
    std::unordered_map<int, BaseMaterial*> made; // indice de Iron3D -> material de C4D
    BaseMaterial* lastMaterial = nullptr;
    C4DExportReport* report = nullptr;
    std::map<std::string, int> warnings;
    std::string outDir;                          // carpeta del .c4d (rutas de textura)
    Float fps = 30.0;
    int lastFrame = 0;
    std::unordered_map<const SceneNode*, BaseObject*> objects;
    std::vector<std::pair<const SceneNode*, BaseObject*>> cameras;
    void warn(const std::string& w) { ++warnings[w]; }
};

cineware::String cs(const std::string& utf8) { return cineware::String(utf8.c_str(), STRINGENCODING_UTF8); }

// Ruta de textura para el .c4d: tal cual si C4D la va a encontrar desde la
// carpeta del archivo; si no, la absoluta ya resuelta (si existe).
std::string texturePathFor(const std::string& path, const iron3d::Material& m, Ctx& ctx) {
    if (path.empty()) return {};
    if (!iron3d::resolveTexturePath(path, ctx.outDir).empty()) return path;
    const std::string abs = iron3d::resolveTexturePath(path, m.texBaseDir);
    if (!abs.empty()) return abs;
    ctx.warn("Texturas que no están en disco: se exporta la ruta tal cual");
    return path;
}

// Shader Imagen con `path` enlazado en `linkId` del material.
void linkBitmap(BaseMaterial* mat, BaseContainer* bc, Int32 linkId, const std::string& path, Ctx& ctx) {
    if (path.empty()) return;
    BaseShader* sh = BaseShader::Alloc(Xbitmap);
    if (!sh) {
        ctx.warn("Texturas: no se pudo crear el shader Imagen");
        return;
    }
    const std::u16string wide = std::filesystem::path(std::u8string(path.begin(), path.end())).u16string();
    sh->SetParameter(BITMAPSHADER_FILENAME, GeData(Filename(cineware::String(reinterpret_cast<const UInt16*>(wide.c_str())))));
    mat->InsertShader(sh);
    bc->SetLink(linkId, sh);
}

BaseMaterial* makeMaterial(int index, Ctx& ctx) {
    if (index < 0 || (size_t)index >= ctx.materials->size()) return nullptr;
    if (auto it = ctx.made.find(index); it != ctx.made.end()) return it->second;
    const iron3d::Material& src = (*ctx.materials)[(size_t)index];

    BaseMaterial* m = BaseMaterial::Alloc(Mmaterial);
    if (!m) return nullptr;
    m->SetName(cs(src.name));
    BaseContainer* bc = m->GetDataInstance();
    auto tex = [&](iron3d::TexSlot slot) { return texturePathFor(src.texture(slot), src, ctx); };
    auto vecOf = [](const glm::vec3& v) { return cineware::Vector(v.x, v.y, v.z); };

    // Las capas originales valen si los campos PBR no se han tocado; si no,
    // se genera una capa que los represente (como antes).
    const bool keepLayers = iron3d::layersMatchPbr(src);
    std::vector<iron3d::ReflectLayer> layers;
    if (keepLayers) {
        for (const iron3d::ReflectLayer& l : src.layers)
            if (!l.transparencyLayer) layers.push_back(l);
    } else {
        layers.push_back(iron3d::layerFromPbr(src));
        if (src.metallic > 0.001f && src.metallic < 0.999f)
            ctx.warn("Materiales medio metálicos: en C4D son un reflejo sin Fresnel con esa fuerza (aproximado)");
    }

    // Color: el de Iron3D (en metal generado, negro: el color va en el reflejo).
    bc->SetBool(MATERIAL_USE_COLOR, true);
    bc->SetVector(MATERIAL_COLOR_COLOR, vecOf(keepLayers ? src.baseColor : iron3d::colorChannelFromPbr(src)));
    bc->SetFloat(MATERIAL_COLOR_BRIGHTNESS, 1.0);
    linkBitmap(m, bc, MATERIAL_COLOR_SHADER, tex(iron3d::TexSlot::Color), ctx);

    const bool emit = src.emission != glm::vec3(0.0f) || !src.texture(iron3d::TexSlot::Emission).empty();
    bc->SetBool(MATERIAL_USE_LUMINANCE, emit);
    if (emit) {
        bc->SetVector(MATERIAL_LUMINANCE_COLOR, vecOf(src.emission));
        bc->SetFloat(MATERIAL_LUMINANCE_BRIGHTNESS, 1.0);
        linkBitmap(m, bc, MATERIAL_LUMINANCE_SHADER, tex(iron3d::TexSlot::Emission), ctx);
    }
    const bool transp = src.transparency != glm::vec3(0.0f) || !src.texture(iron3d::TexSlot::Transparency).empty();
    bc->SetBool(MATERIAL_USE_TRANSPARENCY, transp);
    if (transp) {
        bc->SetVector(MATERIAL_TRANSPARENCY_COLOR, vecOf(src.transparency));
        bc->SetFloat(MATERIAL_TRANSPARENCY_BRIGHTNESS, 1.0);
        bc->SetFloat(MATERIAL_TRANSPARENCY_REFRACTION, src.ior);
        linkBitmap(m, bc, MATERIAL_TRANSPARENCY_SHADER, tex(iron3d::TexSlot::Transparency), ctx);
    }
    const bool normal = !src.texture(iron3d::TexSlot::Normal).empty();
    bc->SetBool(MATERIAL_USE_NORMAL, normal);
    if (normal) {
        bc->SetFloat(MATERIAL_NORMAL_STRENGTH, src.normalStrength);
        linkBitmap(m, bc, MATERIAL_NORMAL_SHADER, tex(iron3d::TexSlot::Normal), ctx);
    }
    const bool bump = !src.texture(iron3d::TexSlot::Bump).empty();
    bc->SetBool(MATERIAL_USE_BUMP, bump);
    if (bump) {
        bc->SetFloat(MATERIAL_BUMP_STRENGTH, src.bumpStrength);
        linkBitmap(m, bc, MATERIAL_BUMP_SHADER, tex(iron3d::TexSlot::Bump), ctx);
    }
    const bool alpha = !src.texture(iron3d::TexSlot::Alpha).empty();
    bc->SetBool(MATERIAL_USE_ALPHA, alpha);
    if (alpha) linkBitmap(m, bc, MATERIAL_ALPHA_SHADER, tex(iron3d::TexSlot::Alpha), ctx);

    // Reflectancia: el material nuevo ya trae una capa; se anaden las que
    // falten y luego se rellenan por indice (AddReflectionLayer las mete
    // arriba, asi que el orden se toma de GetReflectionLayerIndex, el mismo
    // que lee el importador).
    bc->SetBool(MATERIAL_USE_REFLECTION, !layers.empty());
    cineware::Material* cm = static_cast<cineware::Material*>(m);
    auto reflective = [cm] {
        std::vector<ReflectionLayer*> out;
        for (Int32 k = 0; k < cm->GetReflectionLayerCount(); ++k)
            if (ReflectionLayer* c = cm->GetReflectionLayerIndex(k))
                if (!(c->GetFlags() & REFLECTION_FLAG_TRANSPARENCY)) out.push_back(c);
        return out;
    };
    while (reflective().size() < layers.size())
        if (!cm->AddReflectionLayer()) {
            ctx.warn("Materiales: no se pudo crear una capa de reflejo");
            break;
        }
    const std::vector<ReflectionLayer*> slots = reflective();
    for (size_t i = 0; i < layers.size() && i < slots.size(); ++i) {
        ReflectionLayer* layer = slots[i];
        const iron3d::ReflectLayer& l = layers[i];
        const Int32 id = layer->GetDataID();
        layer->SetName(cs(l.name.empty() ? std::string("Capa") : l.name));
        bc->SetInt32(id + REFLECTION_LAYER_MAIN_DISTRIBUTION, l.distribution);
        bc->SetInt32(id + REFLECTION_LAYER_FRESNEL_MODE, l.fresnel);
        bc->SetInt32(id + REFLECTION_LAYER_FRESNEL_PRESET, l.fresnelPreset);
        bc->SetFloat(id + REFLECTION_LAYER_MAIN_VALUE_REFLECTION, l.strength);
        bc->SetFloat(id + REFLECTION_LAYER_MAIN_VALUE_SPECULAR, l.specular);
        bc->SetFloat(id + REFLECTION_LAYER_MAIN_VALUE_ROUGHNESS, l.roughness);
        bc->SetFloat(id + REFLECTION_LAYER_FRESNEL_VALUE_IOR, l.ior);
        bc->SetVector(id + REFLECTION_LAYER_COLOR_COLOR, vecOf(l.color));
        bc->SetFloat(id + REFLECTION_LAYER_COLOR_BRIGHTNESS, 1.0);
        linkBitmap(m, bc, id + REFLECTION_LAYER_MAIN_SHADER_ROUGHNESS, texturePathFor(l.roughnessTex, src, ctx), ctx);
        linkBitmap(m, bc, id + REFLECTION_LAYER_COLOR_TEXTURE, texturePathFor(l.colorTex, src, ctx), ctx);
    }

    ctx.doc->InsertMaterial(m, ctx.lastMaterial); // en el mismo orden que en Iron3D
    ctx.lastMaterial = m;
    ctx.made.emplace(index, m);
    ++ctx.report->materials;
    return m;
}

BaseObject* makePolygonObject(const MeshData& mesh, Ctx& ctx, bool& smooth) {
    const c4d::PolySoup soup = c4d::fromIronMesh(mesh, &smooth);
    if (soup.empty()) return nullptr;
    BaseObject* op = BaseObject::Alloc(Opolygon);
    if (!op) return nullptr;
    PolygonObject* po = static_cast<PolygonObject*>(op);
    if (!po->ResizeObject((Int32)soup.points.size(), (Int32)soup.polys.size())) {
        BaseObject::Free(op);
        return nullptr;
    }
    cineware::Vector* p = po->GetPointW();
    CPolygon* f = po->GetPolygonW();
    for (size_t i = 0; i < soup.points.size(); ++i) p[i] = cv(soup.points[i]);
    for (size_t i = 0; i < soup.polys.size(); ++i) {
        const auto& q = soup.polys[i];
        f[i] = CPolygon(q[0], q[1], q[2], q[3]);
    }
    if (soup.uvs.size() == soup.polys.size()) {
        BaseTag* t = op->MakeTag(Tuvw);
        UVWTag* uvw = static_cast<UVWTag*>(t);
        if (uvw && uvw->Resize((Int32)soup.polys.size())) {
            UVWHandle h = uvw->GetDataAddressW();
            auto v = [](const glm::dvec2& u) { return cineware::Vector(u.x, u.y, 0.0); };
            for (size_t i = 0; i < soup.uvs.size(); ++i) {
                const auto& u = soup.uvs[i];
                UVWTag::Set(h, (Int32)i, UVWStruct(v(u[0]), v(u[1]), v(u[2]), v(u[3])));
            }
        } else {
            ctx.warn("Mallas con UV: no se pudo crear la etiqueta UVW");
        }
    }
    ctx.report->points += (long long)soup.points.size();
    ctx.report->polygons += (long long)soup.polys.size();
    return op;
}

// Pistas de Iron3D -> una pista de C4D por componente. Las tangentes se
// escriben explicitas (las mismas pendientes que AnimTrack::evaluate, con
// asas de 1/4 del tramo, que es lo que C4D trata como tramo lineal en el tiempo,
// medido con Cineware): asi C4D dibuja exactamente la curva de Iron3D. Con
// las tangentes automaticas de C4D la curva se separa entre claves (su regla
// de pendiente no es la de Iron3D), por eso las claves van sin "Auto".
constexpr Float kHandleDiv = 4.0; // asa neutra de C4D: 1/4 del tramo (medido con Cineware)
void exportTracks(const SceneNode& node, BaseObject* op, Ctx& ctx) {
    if (!node.hasAnimation()) return;
    const std::pair<const AnimTrack*, Int32> channels[] = {
        {&node.animPos, ID_BASEOBJECT_REL_POSITION}, {&node.animRot, ID_BASEOBJECT_REL_ROTATION}, {&node.animScale, ID_BASEOBJECT_REL_SCALE}};
    for (const auto& [track, descId] : channels) {
        if (track->empty()) continue;
        for (Int32 c = 0; c < 3; ++c) {
            CTrack* tr = CTrack::Alloc(op, DescID(DescLevel(descId, DTYPE_VECTOR, 0), DescLevel(VECTOR_X + c, DTYPE_REAL, 0)),
                                       cineware::String());
            if (!tr) {
                ctx.warn("Animación: no se pudo crear una pista");
                continue;
            }
            op->InsertTrackSorted(tr);
            CCurve* curve = tr->GetCurve();
            if (!curve) continue;
            const size_t n = track->keys.size();
            std::vector<double> v(n);
            std::vector<double> f(n);
            for (size_t i = 0; i < n; ++i) {
                const AnimKey& k = track->keys[i];
                if (descId == ID_BASEOBJECT_REL_POSITION) {
                    const double s[3] = {k.value.x, k.value.y, -k.value.z};
                    v[i] = s[c] * c4d::kCmPerUnit;
                } else if (descId == ID_BASEOBJECT_REL_ROTATION) {
                    const double r[3] = {k.value.y, k.value.x, k.value.z}; // H, P, B
                    v[i] = r[c] * kPi / 180.0;
                } else {
                    v[i] = k.value[c];
                }
                f[i] = (double)k.frame;
                ctx.lastFrame = std::max(ctx.lastFrame, k.frame);
            }
            std::vector<CKey*> keys(n, nullptr);
            for (size_t i = 0; i < n; ++i) {
                CKey* key = curve->AddKey(BaseTime((Float)f[i], ctx.fps));
                if (!key) continue;
                key->SetValue(curve, v[i]);
                key->SetInterpolation(curve, CINTERPOLATION_SPLINE);
                keys[i] = key;
            }
            for (size_t i = 0; i < n; ++i) {
                CKey* key = keys[i];
                if (!key) continue;
                // Pendiente (valor por fotograma), la misma regla que Iron3D:
                // la de la recta entre vecinas; 0 en extremos y maximos/minimos.
                double slope = 0.0;
                if (i > 0 && i + 1 < n && (v[i] - v[i - 1]) * (v[i + 1] - v[i]) > 0.0)
                    slope = (v[i + 1] - v[i - 1]) / std::max(f[i + 1] - f[i - 1], 1e-6);
                const double spanL = i > 0 ? f[i] - f[i - 1] : (n > 1 ? f[1] - f[0] : 1.0);
                const double spanR = i + 1 < n ? f[i + 1] - f[i] : (n > 1 ? f[i] - f[i - 1] : 1.0);
                key->ChangeNBit(NBIT_CKEY_AUTO, NBITCONTROL_CLEAR);
                // Fraccion exacta (BaseTime(z, n) guarda z/n segundos).
                key->SetTimeLeft(curve, BaseTime((Float)-spanL, kHandleDiv * ctx.fps));
                key->SetTimeRight(curve, BaseTime((Float)spanR, kHandleDiv * ctx.fps));
                key->SetValueLeft(curve, -slope * spanL / kHandleDiv);
                key->SetValueRight(curve, slope * spanR / kHandleDiv);
            }
        }
    }
    ++ctx.report->animated;
}

// Ruta por nombres desde la raiz de la escena ("Grupo/Cubo") -> nodo.
const SceneNode* findByPath(const SceneNode& from, const std::string& path) {
    const SceneNode* root = &from;
    while (root->parent()) root = root->parent();
    const SceneNode* cur = root;
    size_t a = 0;
    while (cur && a <= path.size()) {
        const size_t b = path.find('/', a);
        const std::string part = path.substr(a, b == std::string::npos ? std::string::npos : b - a);
        const SceneNode* next = nullptr;
        for (const auto& c : cur->children())
            if (c->name() == part) {
                next = c.get();
                break;
            }
        cur = next;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return cur == root ? nullptr : cur;
}

BaseObject* exportNode(const SceneNode& node, BaseObject* parent, BaseObject* pred, int parentMaterial,
                       const TextureTagData& parentTag, Ctx& ctx) {
    C4DExportReport& rep = *ctx.report;
    BaseObject* op = nullptr;
    bool smooth = false;
    switch (node.primitive) {
        case PrimitiveType::Cube:
            if ((op = BaseObject::Alloc(Ocube)) != nullptr) {
                op->SetParameter(PRIM_CUBE_LEN, GeData(cineware::Vector(c4d::kCmPerUnit)));
                ++rep.primitives;
            }
            break;
        case PrimitiveType::Sphere:
            if ((op = BaseObject::Alloc(Osphere)) != nullptr) {
                op->SetParameter(PRIM_SPHERE_RAD, GeData(c4d::kCmPerUnit * 0.5));
                ++rep.primitives;
            }
            break;
        case PrimitiveType::Plane:
            if ((op = BaseObject::Alloc(Oplane)) != nullptr) {
                op->SetParameter(PRIM_PLANE_WIDTH, GeData(c4d::kCmPerUnit));
                op->SetParameter(PRIM_PLANE_HEIGHT, GeData(c4d::kCmPerUnit));
                op->SetParameter(PRIM_PLANE_SUBW, GeData((Int32)1));
                op->SetParameter(PRIM_PLANE_SUBH, GeData((Int32)1));
                ++rep.primitives;
            }
            break;
        case PrimitiveType::Light:
            if ((op = BaseObject::Alloc(Olight)) != nullptr) {
                const LightData& l = node.light;
                BaseContainer* bc = op->GetDataInstance();
                const double toRad = kPi / 180.0;
                bc->SetInt32(LIGHT_TYPE, (Int32)l.kind);
                bc->SetVector(LIGHT_COLOR, cv(l.color));
                bc->SetFloat(LIGHT_BRIGHTNESS, l.intensity);
                bc->SetFloat(LIGHT_DETAILS_OUTERANGLE, l.outerAngleDeg * toRad);
                bc->SetFloat(LIGHT_DETAILS_INNERANGLE, l.innerAngleDeg * toRad);
                bc->SetInt32(LIGHT_SHADOWTYPE, l.shadow);
                bc->SetInt32(LIGHT_DETAILS_FALLOFF, l.falloff);
                bc->SetFloat(LIGHT_DETAILS_OUTERDISTANCE, l.falloffRadius * c4d::kCmPerUnit);
                bc->SetFloat(LIGHT_DETAILS_INNERDISTANCE, l.innerRadius * c4d::kCmPerUnit);
                bc->SetInt32(LIGHT_AREADETAILS_SHAPE, l.areaShape);
                bc->SetFloat(LIGHT_AREADETAILS_SIZEX, l.areaSize.x * c4d::kCmPerUnit);
                bc->SetFloat(LIGHT_AREADETAILS_SIZEY, l.areaSize.y * c4d::kCmPerUnit);
                bc->SetFloat(LIGHT_AREADETAILS_SIZEZ, l.areaSize.z * c4d::kCmPerUnit);
                ++rep.lights;
            }
            break;
        case PrimitiveType::Camera:
            if ((op = BaseObject::Alloc(Ocamera)) != nullptr) {
                const CameraData& c = node.camera;
                BaseContainer* bc = op->GetDataInstance();
                bc->SetInt32(CAMERA_PROJECTION, c.projection);
                bc->SetFloat(CAMERA_FOCUS, c.focalMm);
                bc->SetFloat(CAMERAOBJECT_APERTURE, c.sensorMm);
                bc->SetFloat(CAMERA_ZOOM, c.zoom);
                if (!c.target.empty()) ctx.cameras.push_back({&node, op});
                ++rep.cameras;
            }
            break;
        case PrimitiveType::Mesh:
            if (node.mesh) op = makePolygonObject(*node.mesh, ctx, smooth);
            if (op) {
                ++rep.meshes;
            } else {
                ctx.warn("Mallas vacías: se exportan como Nulo");
            }
            break;
        case PrimitiveType::None:
            break;
    }
    if (!op) op = BaseObject::Alloc(Onull);
    if (!op) return nullptr;
    ++rep.objects;

    op->SetName(cineware::String(node.name().c_str(), STRINGENCODING_UTF8));
    // Los numeros de C4D tal cual: posicion en cm (espejo en Z) y HPB en
    // radianes (H = rotationDeg.y, P = .x, B = .z), sin pasar por la matriz.
    const double toRad = kPi / 180.0;
    op->SetRelPos(cineware::Vector(node.position.x * c4d::kCmPerUnit, node.position.y * c4d::kCmPerUnit,
                                   -node.position.z * c4d::kCmPerUnit));
    op->SetRelRot(cineware::Vector(node.rotationDeg.y * toRad, node.rotationDeg.x * toRad, node.rotationDeg.z * toRad));
    op->SetRelScale(cv(node.scale));
    op->SetEditorMode(modeOf(node.editorVis));
    op->SetRenderMode(modeOf(node.renderVis));

    if (smooth) {
        if (BaseTag* ph = op->MakeTag(Tphong)) {
            ph->SetParameter(PHONGTAG_PHONG_ANGLELIMIT, GeData(true));
            ph->SetParameter(PHONGTAG_PHONG_ANGLE, GeData(80.0 * kPi / 180.0));
        }
    }
    // Material: solo donde cambia respecto al padre (en C4D se hereda), o
    // donde cambia la proyeccion/mosaico de la etiqueta.
    if (node.materialIndex >= 0 && (node.materialIndex != parentMaterial || !(node.texTag == parentTag))) {
        if (BaseMaterial* mat = makeMaterial(node.materialIndex, ctx)) {
            if (BaseTag* t = op->MakeTag(Ttexture)) {
                static_cast<TextureTag*>(t)->SetMaterial(mat);
                const TextureTagData& d = node.texTag;
                BaseContainer* bc = t->GetDataInstance();
                bc->SetInt32(TEXTURETAG_PROJECTION, d.projection);
                bc->SetFloat(TEXTURETAG_OFFSETX, d.offset.x);
                bc->SetFloat(TEXTURETAG_OFFSETY, d.offset.y);
                bc->SetFloat(TEXTURETAG_LENGTHX, d.length.x);
                bc->SetFloat(TEXTURETAG_LENGTHY, d.length.y);
                bc->SetBool(TEXTURETAG_TILE, d.tile);
                bc->SetBool(TEXTURETAG_SEAMLESS, d.seamless);
                bc->SetInt32(TEXTURETAG_SIDE, d.side);
            }
        }
    }
    const int material = node.materialIndex >= 0 ? node.materialIndex : parentMaterial;
    const TextureTagData tag = node.materialIndex >= 0 ? node.texTag : parentTag;
    exportTracks(node, op, ctx);
    ctx.objects[&node] = op;

    ctx.doc->InsertObject(op, parent, pred);
    BaseObject* last = nullptr;
    for (const auto& ch : node.children()) last = exportNode(*ch, op, last, material, tag, ctx);
    return op;
}

} // namespace

bool exportC4D(const std::filesystem::path& file, const std::vector<const SceneNode*>& roots,
               const std::vector<iron3d::Material>& materials, bool allMaterials, C4DExportReport& report,
               std::string* outError, int fps) {
    report = C4DExportReport{};
    report.fileName = pathUtf8(file.filename());
    BaseDocument* doc = BaseDocument::Alloc();
    if (!doc) {
        if (outError) *outError = "Cineware no ha podido crear el documento";
        return false;
    }
    Ctx ctx;
    ctx.doc = doc;
    ctx.materials = &materials;
    ctx.report = &report;
    ctx.outDir = pathUtf8(file.parent_path());
    ctx.fps = fps > 0 ? (Float)fps : 30.0;
    doc->SetFps((Int32)ctx.fps);
    BaseObject* last = nullptr;
    for (const SceneNode* r : roots)
        if (r) last = exportNode(*r, nullptr, last, -1, TextureTagData{}, ctx);
    if (allMaterials)
        for (int i = 0; i < (int)materials.size(); ++i) makeMaterial(i, ctx);
    // Etiqueta Objetivo de las camaras (el objetivo tiene que estar exportado).
    for (const auto& [node, op] : ctx.cameras) {
        const SceneNode* target = findByPath(*node, node->camera.target);
        auto it = target ? ctx.objects.find(target) : ctx.objects.end();
        if (it == ctx.objects.end()) {
            ctx.warn("Cámaras con objetivo que no se exporta (o no existe): van sin etiqueta Objetivo");
            continue;
        }
        if (BaseTag* t = op->MakeTag(Ttargetexpression)) t->GetDataInstance()->SetLink(TARGETEXPRESSIONTAG_LINK, it->second);
    }
    if (ctx.lastFrame > 0) doc->SetParameter(DOCUMENT_MAXTIME, GeData(BaseTime((Float)ctx.lastFrame, ctx.fps)));

    // Ruta en UTF-16 (acentos y ñ), como al importar.
    const std::u16string wide = file.u16string();
    Filename fn{cineware::String(reinterpret_cast<const UInt16*>(wide.c_str()))};
    const Bool ok = SaveDocument(doc, fn, SAVEDOCUMENTFLAGS_0);
    BaseDocument::Free(doc);
    if (!ok) {
        if (outError) *outError = "Cineware no ha podido escribir el archivo (¿carpeta sin permiso o archivo abierto?)";
        return false;
    }
    for (const auto& w : ctx.warnings)
        report.warnings.push_back(w.second > 1 ? w.first + " (" + std::to_string(w.second) + ")" : w.first);
    return true;
}

bool exportC4DToBytes(const std::vector<const SceneNode*>& roots, const std::vector<iron3d::Material>& materials,
                      std::vector<uint8_t>& out, C4DExportReport& report, std::string* outError) {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        if (outError) *outError = "No se encuentra la carpeta temporal";
        return false;
    }
    static int counter = 0;
    const std::filesystem::path tmp = dir / ("iron3d_preset_" + std::to_string(++counter) + ".c4d");
    if (!exportC4D(tmp, roots, materials, false, report, outError)) return false;
    std::ifstream in(tmp, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(tmp, ec);
    if (out.empty()) {
        if (outError) *outError = "El .c4d temporal ha salido vacío";
        return false;
    }
    return true;
}

} // namespace iron3d
