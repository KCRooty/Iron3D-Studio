// c4d_probe: importa un .c4d con el mismo importador que Iron3D y muestra el
// arbol resultante y el informe, sin abrir ventana. Para probar el
// importador con archivos reales (solo se compila con IRON3D_CINEWARE_SDK).
//
//   c4d_probe.exe archivo.c4d [--save salida.iron]

#include "core/Paths.h"
#include "core/SceneSerializer.h"
#include "core/ThumbnailRenderer.h"
#include "importers/cineware/CinewareImporter.h"
#include "importers/lib4d/PresetLibraryWriter.h"
#include "importers/lib4d/PresetTextures.h"

#include <fstream>
#include <map>

#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace iron3d;

static void print(const SceneNode& n, const std::vector<Material>& mats, int depth) {
    std::printf("%*s- %s [%s]", depth * 2, "", n.name().c_str(), primitiveTypeName(n.primitive));
    std::printf("  pos=(%.3f %.3f %.3f) rot=(%.1f %.1f %.1f) esc=(%.2f %.2f %.2f)", n.position.x, n.position.y,
                n.position.z, n.rotationDeg.x, n.rotationDeg.y, n.rotationDeg.z, n.scale.x, n.scale.y, n.scale.z);
    if (n.mesh) std::printf("  verts=%zu tris=%zu", n.mesh->vertices.size(), n.mesh->indices.size() / 3);
    if (n.materialIndex >= 0 && (size_t)n.materialIndex < mats.size())
        std::printf("  mat=\"%s\"", mats[(size_t)n.materialIndex].name.c_str());
    if (n.editorVis == VisMode::Off) std::printf("  (oculto)");
    if (n.primitive == PrimitiveType::Light) {
        const LightData& l = n.light;
        std::printf("  luz: tipo=%d color=(%.2f %.2f %.2f) int=%.2f sombra=%d caida=%d radio=%.2f ang=%.0f/%.0f "
                    "noilum=%d dif=%d esp=%d",
                    (int)l.kind, l.color.r, l.color.g, l.color.b, l.intensity, l.shadow, l.falloff, l.falloffRadius,
                    l.innerAngleDeg, l.outerAngleDeg, (int)l.noIllumination, (int)l.diffuse, (int)l.specular);
    }
    if (n.primitive == PrimitiveType::Camera)
        std::printf("  camara: proy=%d focal=%.1f sensor=%.1f zoom=%.2f", n.camera.projection, n.camera.focalMm,
                    n.camera.sensorMm, n.camera.zoom);
    std::printf("\n");
    for (const MaterialPart& p : n.materialParts)
        std::printf("%*s    parte \"%s\": indices %u+%u mat=\"%s\"\n", depth * 2, "", p.selection.c_str(), p.firstIndex,
                    p.indexCount,
                    p.materialIndex >= 0 && (size_t)p.materialIndex < mats.size() ? mats[(size_t)p.materialIndex].name.c_str()
                                                                                   : "-");
    for (const auto& c : n.children()) print(*c, mats, depth + 1);
}

// "--audit lib1.lib4d lib2.lib4d ...": carga cada modelo de cada libreria por
// el mismo camino que la UI (importador + texturas de las librerias) y cuenta
// lo que no llega: texturas perdidas, avisos y shaders de los canales.
static int audit(int argc, wchar_t** argv) {
    std::vector<std::unique_ptr<PresetLibrary>> libs;
    for (int i = 2; i < argc; ++i) {
        auto lib = std::make_unique<PresetLibrary>();
        std::string err;
        if (!lib->open(std::filesystem::path(argv[i]), &err)) {
            std::printf("NO ABRE %s: %s\n", pathUtf8(std::filesystem::path(argv[i])).c_str(), err.c_str());
            continue;
        }
        libs.push_back(std::move(lib));
    }
    std::vector<const PresetLibrary*> all;
    for (auto& l : libs) all.push_back(l.get());
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "Iron3D_audit";
    std::filesystem::create_directories(tmp);
    std::map<std::string, int> shadersAll, warnAll;
    int models = 0, failed = 0, texTotal = 0, texMissing = 0;
    for (auto& lib : libs) {
        for (const PresetItem& it : lib->items()) {
            if (it.kind != PresetKind::Model) continue;
            ++models;
            std::vector<uint8_t> data;
            std::string err;
            if (!lib->readData(it, data, &err) || data.size() < 8) {
                std::printf("  FALLO leer %s/%s: %s\n", lib->title().c_str(), it.path.c_str(), err.c_str());
                ++failed;
                continue;
            }
            const auto file = tmp / "modelo.c4d";
            {
                std::ofstream f(file, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            }
            SceneNode root("Escena");
            std::vector<Material> mats;
            C4DImportReport rep;
            if (!importC4D(file, root, mats, rep, &err)) {
                std::printf("  FALLO importar %s/%s: %s\n", lib->title().c_str(), it.path.c_str(), err.c_str());
                ++failed;
                continue;
            }
            const PresetTextureReport tr = extractPresetTextures(*lib, all, tmp / "texcache", mats, 0);
            int texHere = tr.alreadyOnDisk + tr.extracted + (int)tr.missing.size();
            texTotal += texHere;
            texMissing += (int)tr.missing.size();
            std::printf("%s | %s: %d mat, %d tex (%d de la lib, %d perdidas)\n", lib->title().c_str(), it.path.c_str(),
                        rep.materials, texHere, tr.extracted, (int)tr.missing.size());
            for (const std::string& m : tr.missing) std::printf("    perdida: %s\n", m.c_str());
            for (const auto& [k, v] : rep.shaders) shadersAll[k] += v;
            for (const std::string& w : rep.warnings) ++warnAll[w.substr(0, w.rfind(" ("))];
        }
    }
    std::printf("\n=== %zu librerias, %d modelos (%d fallan), %d texturas, %d perdidas\n", libs.size(), models, failed,
                texTotal, texMissing);
    std::printf("--- shaders en canales (id nombre: veces)\n");
    for (const auto& [k, v] : shadersAll) std::printf("  %5d  %s\n", v, k.c_str());
    std::printf("--- avisos (modelos)\n");
    for (const auto& [k, v] : warnAll) std::printf("  %5d  %s\n", v, k.c_str());
    return 0;
}

int wmain(int argc, wchar_t** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc >= 3 && std::wstring(argv[1]) == L"--audit") return audit(argc, argv);
    if (argc < 2) {
        std::printf("uso: c4d_probe archivo.c4d [--save salida.iron]\n"
                    "     c4d_probe --export escena.iron salida.c4d   (exporta y vuelve a importar)\n");
        return 2;
    }
    // Guardar cada objeto de primer nivel de una escena como preestablecido
    // (con miniatura) en una .lib4d; la crea si no existe. Mismo camino que
    // "Guardar como Preestablecido" en la UI.
    if (argc >= 5 && std::wstring(argv[1]) == L"--preset") {
        SceneNode scene("Escena");
        std::vector<Material> sceneMats;
        std::string err;
        if (!loadScene(std::filesystem::path(argv[2]), scene, sceneMats, &err)) {
            std::printf("ERROR al leer el .iron: %s\n", err.c_str());
            return 1;
        }
        const std::filesystem::path lib(argv[3]);
        const std::string folder = pathUtf8(std::filesystem::path(argv[4]));
        if (!std::filesystem::exists(lib) && !createPresetLibrary(lib, pathUtf8(lib.stem()), &err)) {
            std::printf("ERROR al crear la librería: %s\n", err.c_str());
            return 1;
        }
        std::vector<lib4d::NewEntry> entries;
        for (const auto& c : scene.children()) {
            lib4d::NewEntry e;
            e.path = folder + "/" + c->name();
            C4DExportReport ex;
            if (!exportC4DToBytes({c.get()}, sceneMats, e.data, ex, &err)) {
                std::printf("ERROR al exportar %s: %s\n", c->name().c_str(), err.c_str());
                return 1;
            }
            const Thumbnail th = renderThumbnail({c.get()}, sceneMats, 256);
            e.thumbRgba = th.rgba;
            e.thumbWidth = th.width;
            e.thumbHeight = th.height;
            std::printf("%s: %zu bytes de .c4d, miniatura %dx%d (%d triangulos)\n", e.path.c_str(), e.data.size(),
                        th.width, th.height, th.triangles);
            entries.push_back(std::move(e));
        }
        if (!appendToPresetLibrary(lib, entries, &err)) {
            std::printf("ERROR al añadir: %s\n", err.c_str());
            return 1;
        }
        std::printf("añadidos %zu preestablecidos a %s\n", entries.size(), pathUtf8(lib).c_str());
        return 0;
    }
    if (argc >= 4 && std::wstring(argv[1]) == L"--export") {
        SceneNode scene("Escena");
        std::vector<Material> sceneMats;
        std::string err;
        if (!loadScene(std::filesystem::path(argv[2]), scene, sceneMats, &err)) {
            std::printf("ERROR al leer el .iron: %s\n", err.c_str());
            return 1;
        }
        std::vector<const SceneNode*> roots;
        for (const auto& c : scene.children()) roots.push_back(c.get());
        C4DExportReport ex;
        if (!exportC4D(std::filesystem::path(argv[3]), roots, sceneMats, true, ex, &err)) {
            std::printf("ERROR al exportar: %s\n", err.c_str());
            return 1;
        }
        std::printf("exportado %s: %d objetos, %d mallas, %d primitivas, %d luces, %d materiales, %lld puntos, %lld poligonos\n",
                    ex.fileName.c_str(), ex.objects, ex.meshes, ex.primitives, ex.lights, ex.materials, ex.points,
                    ex.polygons);
        for (const std::string& w : ex.warnings) std::printf("aviso al exportar: %s\n", w.c_str());
        std::printf("--- original (.iron)\n");
        print(scene, sceneMats, 0);
        std::printf("--- releido del .c4d\n");
        argv[1] = argv[3];
        argc = 2;
    }
    SceneNode root("Escena");
    std::vector<Material> mats;
    C4DImportReport rep;
    std::string err;
    if (!importC4D(std::filesystem::path(argv[1]), root, mats, rep, &err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    print(root, mats, 0);
    std::printf("\n%s: %d objetos, %d con malla, %d materiales, %d luces, %lld puntos, %lld poligonos\n",
                rep.fileName.c_str(), rep.objects, rep.meshes, rep.materials, rep.lights, rep.points, rep.polygons);
    std::printf("generadores con cache: %d, primitivas reconstruidas: %d\n", rep.generatorsFromCache,
                rep.primitivesRebuilt);
    for (const Material& m : mats)
        std::printf("material \"%s\": color=(%.2f %.2f %.2f) rugosidad=%.2f metal=%.0f\n", m.name.c_str(), m.baseColor.r,
                    m.baseColor.g, m.baseColor.b, m.roughness, m.metallic);
    for (const std::string& w : rep.warnings) std::printf("aviso: %s\n", w.c_str());
    if (argc >= 4 && std::wstring(argv[2]) == L"--save") {
        if (!saveScene(std::filesystem::path(argv[3]), root, mats, &err)) {
            std::printf("ERROR al guardar: %s\n", err.c_str());
            return 1;
        }
        SceneNode back("Escena");
        std::vector<Material> backMats;
        if (!loadScene(std::filesystem::path(argv[3]), back, backMats, &err)) {
            std::printf("ERROR al releer el .iron: %s\n", err.c_str());
            return 1;
        }
        std::printf(".iron guardado y releido: %zu objetos de primer nivel, %zu materiales\n", back.children().size(),
                    backMats.size());
    }
    return 0;
}
