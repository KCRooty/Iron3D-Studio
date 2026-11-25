// Prueba de FBX (importers/fbx/FbxIO): exportar a FBX ASCII y reimportar con ufbx. Sin GPU.

#include "importers/fbx/FbxIO.h"

#include "core/MeshData.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace iron3d;
namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c);         \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)

static const SceneNode* findMesh(const SceneNode& n) {
    if (n.mesh) return &n;
    for (const auto& c : n.children())
        if (const SceneNode* r = findMesh(*c)) return r;
    return nullptr;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "iron3d_fbx_test";
    fs::create_directories(dir);

    SceneNode root("raiz");
    SceneNode* grp = root.addChild("Grupo");
    grp->position = glm::vec3(1, 2, 3);
    SceneNode* cube = grp->addChild("Caja", PrimitiveType::Cube);
    cube->position = glm::vec3(3, 1, -2);
    cube->rotationDeg = glm::vec3(20.0f, 30.0f, 10.0f);
    cube->materialIndex = 0;
    std::vector<Material> mats(1);
    mats[0].name = "Rojo";
    mats[0].baseColor = {1, 0.1f, 0.1f};

    FbxReport rep;
    std::string err;
    const fs::path out = dir / "caja.fbx";
    CHECK(exportFbx(out, {&root}, mats, rep, &err));
    CHECK(rep.meshes == 1);
    CHECK(rep.polygons == 12);

    SceneNode back("tmp");
    std::vector<Material> mats2;
    FbxReport rep2;
    CHECK(importFbx(out, back, mats2, rep2, &err));
    if (!err.empty()) std::printf("error: %s\n", err.c_str());
    CHECK(rep2.meshes == 1);
    CHECK(rep2.polygons == 12);
    CHECK(mats2.size() == 1);
    if (!mats2.empty()) {
        CHECK(mats2[0].name == "Rojo");
        CHECK(std::fabs(mats2[0].baseColor.x - 1.0f) < 1e-3f);
        CHECK(std::fabs(mats2[0].baseColor.y - 0.1f) < 1e-3f);
    }
    const SceneNode* m = findMesh(back);
    CHECK(m != nullptr);
    if (m) {
        // posicion de mundo de la caja = (1,2,3) + (3,1,-2)
        const glm::vec3 w(m->worldMatrix()[3]);
        CHECK(std::fabs(w.x - 4.0f) < 1e-3f && std::fabs(w.y - 3.0f) < 1e-3f && std::fabs(w.z - 1.0f) < 1e-3f);
        CHECK(m->mesh && m->mesh->indices.size() == 36);
        // La rotacion sobrevive a la ida y vuelta: el eje X local del cubo coincide con el original.
        const glm::mat4 w0 = cube->worldMatrix();
        const glm::mat4 w1 = m->worldMatrix();
        CHECK(std::fabs(w0[0][0] - w1[0][0]) < 1e-3f && std::fabs(w0[1][2] - w1[1][2]) < 1e-3f && std::fabs(w0[2][1] - w1[2][1]) < 1e-3f);
    }

    CHECK(!importFbx(dir / "no_existe.fbx", back, mats2, rep2, &err));

    if (g_fail == 0) std::printf("fbx_test: OK\n");
    return g_fail ? 1 : 0;
}
