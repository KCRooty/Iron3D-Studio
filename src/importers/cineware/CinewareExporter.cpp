// Exportador a .c4d con el Cineware SDK de MAXON (ver CinewareImporter.h).
// Solo se compila con -DIRON3D_CINEWARE_SDK=<ruta>; el SDK no esta en el
// repositorio. Codigo propio de Iron3D escrito sobre la API publica del SDK.
//
// Que se exporta (lo que Iron3D tiene hoy):
// - Jerarquia, nombres, posicion/rotacion/escala y visibilidad de visor/render.
// - Cubo, Esfera y Plano como las primitivas parametricas de C4D (siguen
//   siendo editables alli); Luz como Luz; Nulo como Nulo.
// - Mallas como Objeto Poligonal (vertices unidos y cuadrilateros donde se
//   puede), con etiqueta Phong si estaban suavizadas.
// - Materiales como Material estandar: color y una capa de reflejo GGX con la
//   rugosidad; metal = Fresnel de conductor. Asignados con etiqueta de
//   textura, solo donde cambian respecto al padre (C4D los hereda).
//
// El ID de aplicacion que se escribe en el archivo es el de GetWriterInfo()
// (CinewareImporter.cpp).

#include "CinewareImporter.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "core/Paths.h"

#include "c4d.h"

#include <cstdio>
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
    void warn(const std::string& w) { ++warnings[w]; }
};

BaseMaterial* makeMaterial(int index, Ctx& ctx) {
    if (index < 0 || (size_t)index >= ctx.materials->size()) return nullptr;
    if (auto it = ctx.made.find(index); it != ctx.made.end()) return it->second;
    const iron3d::Material& src = (*ctx.materials)[(size_t)index];

    BaseMaterial* m = BaseMaterial::Alloc(Mmaterial);
    if (!m) return nullptr;
    m->SetName(cineware::String(src.name.c_str(), STRINGENCODING_UTF8));
    BaseContainer* bc = m->GetDataInstance();
    const bool metal = src.metallic >= 0.999f;
    const bool partial = !metal && src.metallic > 0.001f;
    // Metal: el color va en el reflejo y el difuso queda negro, como un
    // metal de verdad en C4D.
    bc->SetBool(MATERIAL_USE_COLOR, true);
    bc->SetVector(MATERIAL_COLOR_COLOR, metal ? cineware::Vector(0.0, 0.0, 0.0) : cv(src.baseColor));
    bc->SetFloat(MATERIAL_COLOR_BRIGHTNESS, 1.0);
    bc->SetBool(MATERIAL_USE_REFLECTION, true);

    cineware::Material* mm = static_cast<cineware::Material*>(m);
    ReflectionLayer* layer = mm->GetReflectionLayerCount() > 0 ? mm->GetReflectionLayerIndex(0) : mm->AddReflectionLayer();
    if (layer) {
        const Int32 id = layer->GetDataID();
        layer->SetName(cineware::String("Iron3D"));
        bc->SetInt32(id + REFLECTION_LAYER_MAIN_DISTRIBUTION, REFLECTION_DISTRIBUTION_GGX);
        bc->SetFloat(id + REFLECTION_LAYER_MAIN_VALUE_ROUGHNESS, src.roughness);
        bc->SetFloat(id + REFLECTION_LAYER_MAIN_VALUE_REFLECTION, partial ? src.metallic : 1.0);
        bc->SetVector(id + REFLECTION_LAYER_COLOR_COLOR, metal || partial ? cv(src.baseColor) : cineware::Vector(1.0, 1.0, 1.0));
        bc->SetFloat(id + REFLECTION_LAYER_COLOR_BRIGHTNESS, 1.0);
        bc->SetInt32(id + REFLECTION_LAYER_FRESNEL_MODE,
                     metal ? REFLECTION_FRESNEL_CONDUCTOR : (partial ? REFLECTION_FRESNEL_NONE : REFLECTION_FRESNEL_DIELECTRIC));
    } else {
        ctx.warn("Materiales: no se pudo crear la capa de reflejo; solo llevan el color");
    }
    if (partial) ctx.warn("Materiales medio metálicos: en C4D son un reflejo sin Fresnel con esa fuerza (aproximado)");

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
    ctx.report->points += (long long)soup.points.size();
    ctx.report->polygons += (long long)soup.polys.size();
    return op;
}

BaseObject* exportNode(const SceneNode& node, BaseObject* parent, BaseObject* pred, int parentMaterial, Ctx& ctx) {
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
            if ((op = BaseObject::Alloc(Olight)) != nullptr) ++rep.lights;
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
    const c4d::Matrix m = c4d::fromIronTRS(node.position, node.rotationDeg, node.scale);
    cineware::Matrix ml;
    ml.off = cv(m.off);
    ml.v1 = cv(m.v1);
    ml.v2 = cv(m.v2);
    ml.v3 = cv(m.v3);
    op->SetMl(ml);
    op->SetEditorMode(modeOf(node.editorVis));
    op->SetRenderMode(modeOf(node.renderVis));

    if (smooth) {
        if (BaseTag* ph = op->MakeTag(Tphong)) {
            ph->SetParameter(PHONGTAG_PHONG_ANGLELIMIT, GeData(true));
            ph->SetParameter(PHONGTAG_PHONG_ANGLE, GeData(80.0 * kPi / 180.0));
        }
    }
    // Material: solo donde cambia respecto al padre (en C4D se hereda).
    if (node.materialIndex >= 0 && node.materialIndex != parentMaterial) {
        if (BaseMaterial* mat = makeMaterial(node.materialIndex, ctx)) {
            if (BaseTag* t = op->MakeTag(Ttexture)) static_cast<TextureTag*>(t)->SetMaterial(mat);
        }
    }
    const int material = node.materialIndex >= 0 ? node.materialIndex : parentMaterial;

    ctx.doc->InsertObject(op, parent, pred);
    BaseObject* last = nullptr;
    for (const auto& ch : node.children()) last = exportNode(*ch, op, last, material, ctx);
    return op;
}

} // namespace

bool exportC4D(const std::filesystem::path& file, const std::vector<const SceneNode*>& roots,
               const std::vector<iron3d::Material>& materials, bool allMaterials, C4DExportReport& report,
               std::string* outError) {
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
    BaseObject* last = nullptr;
    for (const SceneNode* r : roots)
        if (r) last = exportNode(*r, nullptr, last, -1, ctx);
    if (allMaterials)
        for (int i = 0; i < (int)materials.size(); ++i) makeMaterial(i, ctx);

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
