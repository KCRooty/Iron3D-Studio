// Prueba de importar/exportar OBJ (importers/obj/ObjIO). Sin GPU ni Cineware.

#include "importers/obj/ObjIO.h"
#include "core/SceneSerializer.h"

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

int main() {
    const fs::path dir = fs::temp_directory_path() / "iron3d_obj_test";
    fs::create_directories(dir);
    {
        std::ofstream m(dir / "a.mtl");
        m << "newmtl Rojo\nKd 1 0 0\nNs 500\nnewmtl Azul\nKd 0 0 1\nmap_Kd tex/azul.png\n";
        std::ofstream o(dir / "a.obj");
        o << "mtllib a.mtl\n"
             "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 2 1 0\n"
             "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
             "o Cuadro\nusemtl Rojo\nf 1/1 2/2 3/3 4/4\n"
             "usemtl Azul\nf 2/2 5/2 6/3\n"
             "o Otro\nf -4 -3 -2\n";
    }
    SceneNode root("raiz");
    std::vector<Material> mats;
    ObjReport rep;
    std::string err;
    CHECK(importObj(dir / "a.obj", root, mats, rep, &err));
    CHECK(rep.objects == 2);
    CHECK(rep.polygons == 3);
    CHECK(mats.size() == 2);
    CHECK(root.children().size() == 2);
    if (root.children().size() == 2) {
        const SceneNode& n = *root.children()[0];
        CHECK(n.name() == "Cuadro");
        CHECK(n.mesh && n.mesh->faceTris.size() == 2);
        CHECK(n.mesh && n.mesh->faceTris[0] == 2 && n.mesh->faceTris[1] == 1);
        CHECK(n.materialParts.size() == 2);
        CHECK(mats[0].baseColor.x == 1.0f && mats[0].baseColor.z == 0.0f);
        CHECK(mats[1].textures[(int)TexSlot::Color] == "tex/azul.png");
        // v invertida: vt 1 1 -> uv.y = 0
        bool foundFlip = false;
        for (const Vertex& v : n.mesh->vertices)
            if (v.position.x == 1.0f && v.position.y == 1.0f) foundFlip = v.uv.y == 0.0f;
        CHECK(foundFlip);
        // normales calculadas (plano XY -> +Z)
        CHECK(n.mesh->vertices[0].normal.z > 0.99f);
    }

    // Ida y vuelta.
    std::vector<const SceneNode*> roots;
    for (const auto& c : root.children()) roots.push_back(c.get());
    ObjReport rep2;
    CHECK(exportObj(dir / "b.obj", roots, mats, rep2, &err));
    CHECK(rep2.objects == 2 && rep2.polygons == 3);
    CHECK(fs::exists(dir / "b.mtl"));
    SceneNode root2("raiz2");
    std::vector<Material> mats2;
    ObjReport rep3;
    CHECK(importObj(dir / "b.obj", root2, mats2, rep3, &err));
    CHECK(rep3.objects == 2 && rep3.polygons == 3);
    CHECK(mats2.size() == 2);
    if (root2.children().size() == 2 && root2.children()[0]->mesh) {
        const auto& f = root2.children()[0]->mesh->faceTris;
        CHECK(f.size() == 2 && f[0] == 2 && f[1] == 1);
    }

    // Archivo roto.
    {
        std::ofstream o(dir / "mal.obj");
        o << "hola\n";
    }
    SceneNode r3("r3");
    std::vector<Material> m3;
    ObjReport rep4;
    CHECK(!importObj(dir / "mal.obj", r3, m3, rep4, &err));

    // Rig: articulacion + etiqueta IK sobreviven a guardar/abrir y a duplicar.
    {
        SceneNode r("raiz");
        SceneNode* hueso = r.addChild("Hueso");
        hueso->isJoint = true;
        hueso->ik.present = true;
        hueso->ik.enabled = false;
        hueso->ik.solver = 1;
        hueso->ik.tip = "Hueso/Punta";
        hueso->ik.target = "Control IK";
        hueso->ik.pole = "Polo \"rodilla\"";
        hueso->jointLimits.present = true; hueso->jointLimits.use[0] = true; hueso->jointLimits.use[2] = true;
        hueso->nullDisplay.shape = 11; hueso->nullDisplay.radius = 0.75f; hueso->nullDisplay.aspect = 0.5f; hueso->nullDisplay.orientation = 3;
        hueso->jointLimits.minDeg = glm::vec3(-45.5f, 0.0f, -10.0f); hueso->jointLimits.maxDeg = glm::vec3(90.0f, 0.0f, 10.0f); hueso->jointLimits.strength = 0.5f;
        std::vector<Material> none;
        CHECK(saveScene(dir / "rig.iron", r, none, &err));
        SceneNode r2("otra");
        std::vector<Material> none2;
        CHECK(loadScene(dir / "rig.iron", r2, none2, &err));
        CHECK(r2.children().size() == 1);
        if (r2.children().size() == 1) {
            const SceneNode& h = *r2.children()[0];
            CHECK(h.isJoint);
            CHECK(h.ik == hueso->ik);
            CHECK(h.jointLimits == hueso->jointLimits);
            CHECK(h.nullDisplay == hueso->nullDisplay);
        }
        SceneNode* copia = duplicateSubtree(r, *hueso);
        CHECK(copia && copia->isJoint && copia->ik == hueso->ik && copia->jointLimits == hueso->jointLimits);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    if (g_fail == 0) std::printf("obj: todo correcto\n");
    return g_fail == 0 ? 0 : 1;
}
