// c4d_roundtrip: ida y vuelta Iron3D -> .c4d (Cineware) -> Iron3D con la escena
// de io_scene_fixture.h (materiales con todos los canales y texturas, capas de
// Reflectancia, etiquetas de textura, luces, camaras con objetivo, jerarquia,
// visibilidad, HPB y pistas PSR). Compara numeros, no solo que abra.
//
//   c4d_roundtrip_test                 exporta a %TEMP%, reimporta y compara
//   c4d_roundtrip_test --write a.c4d   solo exporta (con tex/ al lado)
//   c4d_roundtrip_test --compare a.c4d reimporta un .c4d (p. ej. re-guardado
//                                      por Cinema 4D R19) y lo compara
//
// Ademas evalua las curvas con Cineware (la misma spline que usa C4D) en
// todos los fotogramas y las compara con AnimTrack::evaluate de Iron3D.

#include "io_scene_fixture.h"

#include "core/Paths.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "importers/cineware/CinewareImporter.h"

#include "c4d.h"

#include <stb_image_write.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace iron3d;

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "  ok  " : "  FALLA", what);
    if (!ok) ++g_failures;
}

const char* kTextures[] = {"color.png", "normal.png", "bump.png", "alpha.png", "emit.png", "rough.png", "metal.png",
                           "capa.png"};

void writeTextures(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir / "tex");
    for (const char* name : kTextures) {
        unsigned char px[4 * 4 * 3];
        for (int i = 0; i < 16; ++i) {
            px[i * 3 + 0] = (unsigned char)(i * 16);
            px[i * 3 + 1] = (unsigned char)(255 - i * 16);
            px[i * 3 + 2] = (unsigned char)((i % 2) * 255);
        }
        std::ofstream f(dir / "tex" / name, std::ios::binary | std::ios::trunc);
        stbi_write_png_to_func(
            [](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write((const char*)data, size); },
            &f, 4, 4, 3, px, 4 * 3);
    }
}

bool exportFixture(const std::filesystem::path& file) {
    SceneNode root("Raiz");
    std::vector<Material> mats;
    testio::buildScene(root, mats, pathUtf8(file.parent_path()));
    writeTextures(file.parent_path());
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    C4DExportReport rep;
    std::string err;
    const bool ok = exportC4D(file, roots, mats, true, rep, &err, 30);
    std::printf("exportado %s: %d objetos, %d luces, %d camaras, %d materiales, %d animados%s%s\n",
                pathUtf8(file).c_str(), rep.objects, rep.lights, rep.cameras, rep.materials, rep.animated,
                ok ? "" : " ERROR: ", err.c_str());
    for (const std::string& w : rep.warnings) std::printf("  aviso al exportar: %s\n", w.c_str());
    return ok;
}

std::string utf8(const cineware::String& s) {
    cineware::Char* c = s.GetCStringCopy(cineware::STRINGENCODING_UTF8);
    std::string out = c ? c : "";
    cineware::DeleteMem(c);
    return out;
}

cineware::BaseObject* findObject(cineware::BaseObject* op, const std::string& name) {
    for (; op; op = op->GetNext()) {
        if (utf8(op->GetName()) == name) return op;
        if (cineware::BaseObject* r = findObject(op->GetDown(), name)) return r;
    }
    return nullptr;
}

// Curvas de C4D frente a AnimTrack::evaluate, fotograma a fotograma.
void compareCurves(const std::filesystem::path& file, const SceneNode& cubo) {
    using namespace cineware;
    const std::u16string wide = file.u16string();
    Filename fn{String(reinterpret_cast<const UInt16*>(wide.c_str()))};
    BaseDocument* doc = LoadDocument(fn, SCENEFILTER_OBJECTS);
    check(doc != nullptr, "curvas: Cineware abre el archivo");
    if (!doc) return;
    if (BaseObject* malla = findObject(doc->GetFirstObject(), "Malla UV")) {
        BaseTag* t = malla->GetTag(Tuvw);
        const Int32 polys = malla->GetType() == Opolygon ? static_cast<PolygonObject*>(malla)->GetPolygonCount() : -1;
        const Int32 data = t ? static_cast<UVWTag*>(t)->GetDataCount() : -1;
        std::printf("  Malla UV en el .c4d: %d poligonos, etiqueta UVW con %d\n", polys, data);
        if (t && data > 0) {
            UVWStruct u;
            UVWTag::Get(static_cast<UVWTag*>(t)->GetDataAddressR(), 0, u);
            std::printf("    poligono 0: a=(%g %g) b=(%g %g) c=(%g %g) d=(%g %g)\n", u.a.x, u.a.y, u.b.x, u.b.y, u.c.x,
                        u.c.y, u.d.x, u.d.y);
        }
        check(t && data == polys && polys > 0, "uv: la etiqueta UVW esta en el .c4d con una entrada por poligono");
    }
    BaseObject* op = findObject(doc->GetFirstObject(), "Cubo");
    check(op != nullptr, "curvas: esta el Cubo");
    if (op) {
        const Float fps = doc->GetFps() > 0 ? (Float)doc->GetFps() : 30.0;
        const Int32 ids[3] = {ID_BASEOBJECT_REL_POSITION, ID_BASEOBJECT_REL_ROTATION, ID_BASEOBJECT_REL_SCALE};
        const AnimTrack* tracks[3] = {&cubo.animPos, &cubo.animRot, &cubo.animScale};
        const char* names[3] = {"posicion", "rotacion", "escala"};
        for (int ch = 0; ch < 3; ++ch) {
            CCurve* cv[3] = {};
            for (CTrack* tr = op->GetFirstCTrack(); tr; tr = tr->GetNext()) {
                const DescID& id = tr->GetDescriptionID();
                if (id.GetDepth() >= 2 && id[0].id == ids[ch] && id[1].id >= VECTOR_X && id[1].id <= VECTOR_Z)
                    cv[id[1].id - VECTOR_X] = tr->GetCurve(CCURVE_CURVE, false);
            }
            if (std::getenv("IRON3D_TANGENTS"))
                for (int b = 0; b < 3; ++b)
                    if (cv[b])
                        for (Int32 k = 0; k < cv[b]->GetKeyCount(); ++k) {
                            Float64 vl, vr, tl, tr;
                            cv[b]->GetTangents(k, &vl, &vr, &tl, &tr);
                            const CKey* key = cv[b]->GetKey(k);
                            std::printf("    %s[%d] clave %d (f=%d v=%g): vl=%g tl=%g vr=%g tr=%g auto=%d modo=%d\n",
                                        names[ch], b, k, (int)key->GetTime().GetFrame(fps), key->GetValue(), vl, tl, vr,
                                        tr, key->GetNBit(NBIT_CKEY_AUTO) ? 1 : 0, (int)key->GetAutomaticTangentMode());
                        }
            double maxErr = 0.0;
            int worstFrame = 0;
            for (int f = -5; f <= 50; ++f) {
                double v[3];
                for (int b = 0; b < 3; ++b) v[b] = cv[b] ? cv[b]->GetValue(BaseTime((Float)f, fps)) : 0.0;
                glm::dvec3 iron;
                if (ch == 0) iron = {v[0] / c4d::kCmPerUnit, v[1] / c4d::kCmPerUnit, -v[2] / c4d::kCmPerUnit};
                else if (ch == 1) iron = {v[1] * 180.0 / 3.14159265358979323846, v[0] * 180.0 / 3.14159265358979323846, v[2] * 180.0 / 3.14159265358979323846};
                else iron = {v[0], v[1], v[2]};
                const glm::vec3 want = tracks[ch]->evaluate((float)f);
                for (int b = 0; b < 3; ++b) {
                    const double e = std::fabs(iron[b] - (double)want[b]);
                    if (e > maxErr) {
                        maxErr = e;
                        worstFrame = f;
                    }
                }
            }
            std::printf("  curvas de %s: error maximo %.6g (fotograma %d)\n", names[ch], maxErr, worstFrame);
            const bool allCurves = cv[0] && cv[1] && cv[2];
            char what[128];
            std::snprintf(what, sizeof what, "curvas de %s: C4D evalua igual que Iron3D en los fotogramas -5..50", names[ch]);
            check(allCurves && maxErr < (ch == 1 ? 1e-2 : 1e-4), what);
        }
    }
    BaseDocument::Free(doc);
}

bool compareFile(const std::filesystem::path& file) {
    SceneNode ref("Raiz");
    std::vector<Material> refMats;
    testio::buildScene(ref, refMats, pathUtf8(file.parent_path()));

    SceneNode back("Raiz");
    std::vector<Material> mats;
    C4DImportReport rep;
    std::string err;
    const bool ok = importC4D(file, back, mats, rep, &err);
    check(ok, "importar el .c4d");
    if (!ok) {
        std::printf("  %s\n", err.c_str());
        return false;
    }
    std::printf("importado: %d objetos, %d luces, %d camaras, %d materiales, %d texturas (%d sin encontrar), %d animados\n",
                rep.objects, rep.lights, rep.cameras, rep.materials, rep.textures, rep.texturesMissing, rep.animated);
    for (const std::string& w : rep.warnings) std::printf("  aviso al importar: %s\n", w.c_str());
    check(rep.texturesMissing == 0, "texturas: todas se encuentran junto al .c4d (tex/)");
    check(rep.cameras == 2 && rep.lights == 4, "luces y camaras: vuelven todas");

    testio::CompareOptions o;
    o.tol = 1e-4f;
    o.meshExact = false;
    o.layersFromPbr = true;
    o.primitivesBecomeMesh = true;
    const testio::Diffs d = testio::compareScenes(ref, refMats, back, mats, o);
    for (const std::string& s : d.list) std::printf("  diferencia: %s\n", s.c_str());
    check(d.list.empty(), "escena: materiales, texturas, luces, camaras, jerarquia, HPB, visibilidad y claves iguales");

    // Las rutas de textura deben resolver a archivos que existen.
    int resolved = 0, total = 0;
    for (const Material& m : mats)
        for (const std::string& t : m.textures)
            if (!t.empty()) {
                ++total;
                if (!resolveTexturePath(t, m.texBaseDir).empty()) ++resolved;
            }
    std::printf("  texturas en los materiales: %d de %d en disco\n", resolved, total);
    check(total > 0 && resolved == total, "texturas: las rutas importadas apuntan a archivos reales");

    const SceneNode* grupo = nullptr;
    for (const auto& c : ref.children())
        if (c->name() == "Grupo") grupo = c.get();
    const SceneNode* cubo = nullptr;
    if (grupo)
        for (const auto& c : grupo->children())
            if (c->name() == "Cubo") cubo = c.get();
    if (cubo) compareCurves(file, *cubo);
    return g_failures == 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && std::wstring(argv[1]) == L"--write") return exportFixture(argv[2]) ? 0 : 1;
    if (argc >= 3 && std::wstring(argv[1]) == L"--compare") {
        compareFile(argv[2]);
    } else {
        const auto dir = std::filesystem::temp_directory_path() / "iron3d_c4d_roundtrip";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
        const auto file = dir / "ida_y_vuelta.c4d";
        check(exportFixture(file), "exportar el .c4d");
        if (g_failures == 0) compareFile(file);
    }
    if (g_failures == 0) std::printf("c4d_roundtrip: todo correcto\n");
    return g_failures == 0 ? 0 : 1;
}
