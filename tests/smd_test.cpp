// Prueba del exportador Valve SMD (importers/smd/SmdIO). Sin GPU ni Cineware.

#include "importers/smd/SmdIO.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace iron3d;
namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(c)                                                      \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                 \
        }                                                             \
    } while (0)

int main() {
    const fs::path dir = fs::temp_directory_path() / "iron3d_smd_test";
    fs::create_directories(dir);

    Material m;
    m.name = "Pared Roja";
    m.textures[(int)TexSlot::Color] = "C:/tex/Ladrillo Viejo.PNG";
    CHECK(smdMaterialName(m) == "ladrillo_viejo");
    m.textures[(int)TexSlot::Color].clear();
    CHECK(smdMaterialName(m) == "pared_roja");

    SceneNode root("raiz");
    SceneNode* cube = root.addChild("Cubo", PrimitiveType::Cube);
    cube->materialIndex = 0;
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());

    SmdReport rep;
    std::string err;
    const fs::path out = dir / "cubo.smd";
    CHECK(exportSmd(out, roots, {m}, rep, &err, 1.0f));
    CHECK(rep.objects == 1);
    CHECK(rep.triangles == 12);
    CHECK(rep.materials.size() == 1 && rep.materials[0] == "pared_roja");

    std::ifstream in(out);
    std::string line;
    int vertexLines = 0, matLines = 0;
    float maxZ = -1e9f, minZ = 1e9f;  // Iron3D Y arriba -> Source Z arriba
    bool header = false, trianglesBlock = false;
    while (std::getline(in, line)) {
        if (line == "version 1") header = true;
        if (line == "triangles") { trianglesBlock = true; continue; }
        if (!trianglesBlock || line == "end") continue;
        if (line == "pared_roja") { ++matLines; continue; }
        std::istringstream s(line);
        int bone;
        float px, py, pz;
        if (s >> bone >> px >> py >> pz) {
            ++vertexLines;
            maxZ = std::max(maxZ, pz);
            minZ = std::min(minZ, pz);
        }
    }
    CHECK(header);
    CHECK(matLines == 12);
    CHECK(vertexLines == 36);
    // El cubo por defecto mide 200 cm centrado en el origen: Z va de -100 a 100.
    CHECK(std::fabs(maxZ - 100.0f) < 1.0f && std::fabs(minZ + 100.0f) < 1.0f);

    // Escena sin geometria: falla con mensaje.
    SceneNode vacio("v");
    std::vector<const SceneNode*> none;
    SmdReport r2;
    CHECK(!exportSmd(dir / "vacio.smd", none, {}, r2, &err));

    if (g_fail == 0) std::printf("smd: todo correcto\n");
    return g_fail == 0 ? 0 : 1;
}
