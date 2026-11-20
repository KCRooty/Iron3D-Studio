// Prueba del exportador Valve SMD (importers/smd/SmdIO). Sin GPU ni Cineware.

#include "importers/smd/SmdIO.h"

#include "core/Skin.h"

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

    // Esqueleto y pesos: Cadera (sube 1 unidad = 200 cm) > Rodilla (H = 90), y una malla con Piel.
    {
        SceneNode r2("r2");
        SceneNode* cadera = r2.addChild("Cadera");
        cadera->isJoint = true;
        cadera->position = glm::vec3(0, 1, 0);
        SceneNode* rodilla = cadera->addChild("Rodilla");
        rodilla->isJoint = true;
        rodilla->rotationDeg = glm::vec3(0, 90, 0);
        SceneNode* malla = r2.addChild("Malla", PrimitiveType::Mesh);
        auto md = std::make_shared<MeshData>();
        const glm::vec3 pts[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        for (const glm::vec3& p : pts) {
            Vertex v{};
            v.position = p;
            v.normal = glm::vec3(0, 0, 1);
            md->vertices.push_back(v);
        }
        md->indices = {0, 1, 2};
        malla->mesh = md;
        auto sk = std::make_shared<SkinData>();
        sk->jointPaths = {"Cadera", "Cadera/Rodilla"};
        sk->bindMesh = md;
        sk->influences.resize(3);
        sk->influences[0].joint[0] = 0; sk->influences[0].weight[0] = 1.0f;
        sk->influences[1].joint[0] = 0; sk->influences[1].weight[0] = 0.5f;
        sk->influences[1].joint[1] = 1; sk->influences[1].weight[1] = 0.5f;
        // el tercero sin pesos: rigido (la malla no esta bajo ninguna articulacion: root)
        malla->skin = sk;

        std::vector<const SceneNode*> rs;
        for (const auto& c : r2.children()) rs.push_back(c.get());
        SmdReport rr;
        const fs::path out2 = dir / "rig.smd";
        CHECK(exportSmd(out2, rs, {}, rr, &err, 1.0f));
        CHECK(rr.bones == 3);
        CHECK(rr.skinnedMeshes == 1);

        std::ifstream in2(out2);
        std::string ln;
        std::vector<std::string> nodesL, skelL, triL;
        std::string sect;
        while (std::getline(in2, ln)) {
            if (ln == "nodes" || ln == "skeleton" || ln == "triangles") { sect = ln; continue; }
            if (ln == "end") { sect.clear(); continue; }
            if (sect == "nodes") nodesL.push_back(ln);
            else if (sect == "skeleton" && ln != "time 0") skelL.push_back(ln);
            else if (sect == "triangles" && ln != "no_material") triL.push_back(ln);
        }
        CHECK(nodesL.size() == 3);
        CHECK(nodesL.size() == 3 && nodesL[0] == "0 \"root\" -1" && nodesL[1] == "1 \"cadera\" 0" && nodesL[2] == "2 \"rodilla\" 1");
        CHECK(skelL.size() == 3);
        if (skelL.size() == 3) {
            std::istringstream s1(skelL[1]), s2(skelL[2]);
            int b;
            float px, py, pz, rx, ry, rz;
            CHECK((bool)(s1 >> b >> px >> py >> pz >> rx >> ry >> rz));
            CHECK(std::fabs(px) < 1e-3f && std::fabs(py) < 1e-3f && std::fabs(pz - 200.0f) < 0.1f);  // Y arriba -> Z arriba
            CHECK((bool)(s2 >> b >> px >> py >> pz >> rx >> ry >> rz));
            CHECK(std::fabs(px) + std::fabs(py) + std::fabs(pz) < 1e-3f);  // la Rodilla esta en el mismo sitio
            CHECK(std::fabs(std::fabs(rz) - 1.5707963f) < 1e-3f && std::fabs(rx) < 1e-3f && std::fabs(ry) < 1e-3f);  // H = 90: giro en Z
        }
        CHECK(triL.size() == 3);
        if (triL.size() == 3) {
            auto tok = [](const std::string& l) {
                std::istringstream s(l);
                std::vector<std::string> v;
                std::string t;
                while (s >> t) v.push_back(t);
                return v;
            };
            // El orden de los vertices esta invertido: ids {0, 2, 1}.
            const auto t0 = tok(triL[0]), t1 = tok(triL[1]), t2 = tok(triL[2]);
            CHECK(t0.size() == 12 && t0[0] == "1" && t0[9] == "1" && t0[10] == "1" && std::fabs(std::stof(t0[11]) - 1.0f) < 1e-4f);  // v0: Cadera 1.0
            CHECK(t1.size() == 12 && t1[0] == "0" && t1[10] == "0");                                                                // v2: sin pesos, todo a root
            CHECK(t2.size() == 14 && t2[9] == "2");                                                                                   // v1: dos huesos
            if (t2.size() == 14) CHECK(std::fabs(std::stof(t2[11]) - 0.5f) < 1e-4f && std::fabs(std::stof(t2[13]) - 0.5f) < 1e-4f);
        }
    }

    // Escena sin geometria: falla con mensaje.
    SceneNode vacio("v");
    std::vector<const SceneNode*> none;
    SmdReport r2;
    CHECK(!exportSmd(dir / "vacio.smd", none, {}, r2, &err));

    if (g_fail == 0) std::printf("smd: todo correcto\n");
    return g_fail == 0 ? 0 : 1;
}
