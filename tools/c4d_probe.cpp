// c4d_probe: importa un .c4d con el mismo importador que Iron3D y muestra el
// arbol resultante y el informe, sin abrir ventana. Para probar el
// importador con archivos reales (solo se compila con IRON3D_CINEWARE_SDK).
//
//   c4d_probe.exe archivo.c4d [--save salida.iron]

#include "core/SceneSerializer.h"
#include "importers/cineware/CinewareImporter.h"

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
    std::printf("\n");
    for (const auto& c : n.children()) print(*c, mats, depth + 1);
}

int wmain(int argc, wchar_t** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        std::printf("uso: c4d_probe archivo.c4d [--save salida.iron]\n");
        return 2;
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
