// Prueba de glTF/GLB (importers/gltf/GltfIO): exportar y reimportar, y un .gltf escrito a mano
// con buffer base64. Sin GPU.

#include "importers/gltf/GltfIO.h"

#include "core/MeshData.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

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
    const fs::path dir = fs::temp_directory_path() / "iron3d_gltf_test";
    fs::create_directories(dir);

    // Ida y vuelta por .glb.
    {
        SceneNode root("raiz");
        SceneNode* cube = root.addChild("Caja", PrimitiveType::Cube);
        cube->position = glm::vec3(3, 1, -2);
        cube->materialIndex = 0;
        std::vector<Material> mats(1);
        mats[0].name = "Rojo";
        mats[0].baseColor = {1, 0.1f, 0.1f};
        mats[0].metallic = 0.25f;
        mats[0].roughness = 0.75f;
        GltfReport rep;
        std::string err;
        const fs::path out = dir / "caja.glb";
        CHECK(exportGlb(out, {&root}, mats, rep, &err));
        CHECK(rep.meshes == 1 && rep.polygons == 12);
        CHECK(isGltfPath(out));

        SceneNode back("destino");
        std::vector<Material> mats2;
        GltfReport rep2;
        CHECK(importGltf(out, back, mats2, rep2, &err));
        const SceneNode* m = findMesh(back);
        CHECK(m != nullptr);
        if (m) {
            CHECK(m->mesh->indices.size() == 36);
            CHECK(std::abs(m->worldMatrix()[3].x - 3.0f) < 1e-4f);
            CHECK(std::abs(m->worldMatrix()[3].z + 2.0f) < 1e-4f);
            CHECK(m->materialIndex == 0);
        }
        CHECK(mats2.size() == 1);
        if (!mats2.empty()) {
            CHECK(mats2[0].name == "Rojo");
            CHECK(std::abs(mats2[0].metallic - 0.25f) < 1e-5f);
            CHECK(std::abs(mats2[0].roughness - 0.75f) < 1e-5f);
            CHECK(std::abs(mats2[0].baseColor.r - 1.0f) < 1e-5f);
        }
    }

    // .gltf a mano: un triangulo con buffer en base64 (3 posiciones, indices implicitos).
    {
        // 3 vec3 float: (0,0,0) (1,0,0) (0,1,0)
        const char* gltf =
            "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
            "\"nodes\":[{\"name\":\"Tri\",\"mesh\":0,\"translation\":[0,5,0]}],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
            "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,"
            "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\"}]}";
        const fs::path p = dir / "tri.gltf";
        std::ofstream(p, std::ios::binary) << gltf;
        SceneNode root("r");
        std::vector<Material> mats;
        GltfReport rep;
        std::string err;
        CHECK(importGltf(p, root, mats, rep, &err));
        const SceneNode* m = findMesh(root);
        CHECK(m != nullptr);
        if (m) {
            CHECK(m->mesh->vertices.size() == 3 && m->mesh->indices.size() == 3);
            CHECK(std::abs(m->worldMatrix()[3].y - 5.0f) < 1e-5f);
            CHECK(std::abs(m->mesh->vertices[1].position.x - 1.0f) < 1e-5f);
            CHECK(std::abs(m->mesh->vertices[0].normal.z) > 0.9f); // normal calculada
        }
    }

    // Archivos rotos no tumban nada.
    {
        const fs::path p = dir / "roto.glb";
        std::ofstream(p, std::ios::binary) << "glTF\x02\x00\x00\x00basura";
        SceneNode root("r");
        std::vector<Material> mats;
        GltfReport rep;
        std::string err;
        CHECK(!importGltf(p, root, mats, rep, &err));
        CHECK(!err.empty());
        const fs::path q = dir / "roto.gltf";
        std::ofstream(q) << "{ no es json";
        CHECK(!importGltf(q, root, mats, rep, &err));
    }

    fs::remove_all(dir);
    if (g_fail == 0) std::printf("gltf: todo correcto\n");
    return g_fail == 0 ? 0 : 1;
}
