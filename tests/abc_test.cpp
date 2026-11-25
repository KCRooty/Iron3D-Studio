// Prueba de Alembic (importers/abc/AbcIO): exportar un .abc Ogawa y reimportarlo. Sin GPU.

#include "importers/abc/AbcIO.h"

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
    const fs::path dir = fs::temp_directory_path() / "iron3d_abc_test";
    fs::create_directories(dir);

    SceneNode root("raiz");
    SceneNode* grp = root.addChild("Grupo");
    grp->position = glm::vec3(1, 2, 3);
    SceneNode* cube = grp->addChild("Caja", PrimitiveType::Cube);
    cube->position = glm::vec3(3, 1, -2);
    cube->rotationDeg = glm::vec3(20.0f, 30.0f, 10.0f);
    std::vector<Material> mats;

    AbcReport rep;
    std::string err;
    const fs::path out = dir / "caja.abc";
    CHECK(exportAbc(out, {&root}, mats, rep, &err));
    if (!err.empty()) std::printf("error: %s\n", err.c_str());
    CHECK(rep.meshes == 1);
    CHECK(rep.polygons == 12);

    SceneNode back("tmp");
    AbcReport rep2;
    CHECK(importAbc(out, back, mats, rep2, &err));
    if (!err.empty()) std::printf("error: %s\n", err.c_str());
    CHECK(rep2.meshes == 1);
    CHECK(rep2.polygons == 12);
    const SceneNode* m = findMesh(back);
    CHECK(m != nullptr);
    if (m) {
        const glm::vec3 w(m->worldMatrix()[3]);
        CHECK(std::fabs(w.x - 4.0f) < 1e-3f && std::fabs(w.y - 3.0f) < 1e-3f && std::fabs(w.z - 1.0f) < 1e-3f);
        CHECK(m->mesh && m->mesh->indices.size() == 36);
        const glm::mat4 w0 = cube->worldMatrix();
        const glm::mat4 w1 = m->worldMatrix();
        CHECK(std::fabs(w0[0][0] - w1[0][0]) < 1e-3f && std::fabs(w0[1][2] - w1[1][2]) < 1e-3f);
        // El sentido de las caras sobrevive: normal geometrica del primer triangulo = normal del vertice.
        if (m->mesh && m->mesh->indices.size() >= 3) {
            const auto& V = m->mesh->vertices;
            const auto& I = m->mesh->indices;
            const glm::vec3 gn = glm::cross(V[I[1]].position - V[I[0]].position, V[I[2]].position - V[I[0]].position);
            const MeshData orig = makeCubeMesh();
            const glm::vec3 gn0 = glm::cross(orig.vertices[orig.indices[1]].position - orig.vertices[orig.indices[0]].position,
                                             orig.vertices[orig.indices[2]].position - orig.vertices[orig.indices[0]].position);
            // Misma relacion entre cara y normal que en el cubo original.
            CHECK((glm::dot(gn, V[I[0]].normal) > 0.0f) == (glm::dot(gn0, orig.vertices[orig.indices[0]].normal) > 0.0f));
        }
    }

    CHECK(!importAbc(dir / "no_existe.abc", back, mats, rep2, &err));

    if (g_fail == 0) std::printf("abc_test: OK\n");
    return g_fail ? 1 : 0;
}
