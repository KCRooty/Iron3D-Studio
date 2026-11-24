// Pistas de parametros, Nivel de Puntos y splines: evaluacion y guardado/carga (2026-11-24).
#include "core/Deform.h"
#include "core/Material.h"
#include "core/MeshData.h"
#include "core/SceneNode.h"
#include "core/SceneSerializer.h"
#include "core/Spline.h"

#include <cmath>
#include <cstdio>

using namespace iron3d;

static int g_fail = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                  \
        }                                                              \
    } while (0)

int main() {
    SceneNode root("root");
    // Deformador con pista de Fuerza
    SceneNode* d = root.addChild("Doblar");
    d->deform.present = true;
    d->deform.kind = 0;
    d->deform.amount = 0.0f;
    CHECK(recordParamKeys(*d, 0) == 2);
    d->deform.amount = 100.0f;
    recordParamKeys(*d, 10);
    evaluateAnimation(root, 5.0f);
    CHECK(d->deform.amount > 10.0f && d->deform.amount < 90.0f);
    evaluateAnimation(root, 10.0f);
    CHECK(std::fabs(d->deform.amount - 100.0f) < 1e-4f);
    // Seleccion de Claves: solo pistas existentes
    SceneNode* l = root.addChild("Luz", PrimitiveType::Light);
    CHECK(recordParamKeys(*l, 0, true) == 0);
    CHECK(recordParamKeys(*l, 0) == 5);

    // PLA
    SceneNode* m = root.addChild("Malla", PrimitiveType::Mesh);
    MeshData cube = makeCubeMesh();
    updateBounds(cube);
    m->mesh = std::make_shared<const MeshData>(cube);
    CHECK(recordPlaKey(*m, 0));
    MeshData big = cube;
    for (Vertex& v : big.vertices) v.position *= 3.0f;
    m->mesh = std::make_shared<const MeshData>(big);
    CHECK(recordPlaKey(*m, 20));
    evaluateAnimation(root, 10.0f);
    CHECK(std::fabs(m->mesh->vertices[0].position.x - cube.vertices[0].position.x * 2.0f) < 1e-4f);
    const std::vector<int> kf = keyFrames(*m);
    CHECK(kf.size() == 2 && kf[0] == 0 && kf[1] == 20);
    CHECK(moveKeys(*m, {20}, 5) == 1 && m->animPla.back().frame == 25);
    setKeyInterp(*m, {0}, KeyInterp::Step);
    evaluateAnimation(root, 10.0f);
    CHECK(std::fabs(m->mesh->vertices[0].position.x - cube.vertices[0].position.x) < 1e-4f);

    // Spline
    SceneNode* s = root.addChild("Estrella");
    s->spline = defaultSpline(6);
    s->spline.a = 333.0f;
    SceneNode* f = root.addChild("Libre");
    f->spline = defaultSpline(0);
    f->spline.points = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}};
    f->spline.extra = {{{7, 8, 9}, {1, 1, 1}}};
    f->spline.extraClosed = {1};
    f->spline.text = "con espacios y \"comillas\"";

    std::vector<Material> mats;
    const std::string txt = serializeScene(root, mats);
    SceneNode back("root");
    std::vector<Material> mats2;
    std::string err;
    CHECK(deserializeScene(txt, back, mats2, &err));
    if (!err.empty()) std::printf("error: %s\n", err.c_str());
    if (back.children().size() == root.children().size()) {
        const SceneNode& d2 = *back.children()[0];
        CHECK(d2.animParams.size() == 2 && d2.animParams[0].id == "def.amount" && d2.animParams[0].track.keys.size() == 2);
        const SceneNode& m2 = *back.children()[2];
        CHECK(m2.animPla.size() == 2 && m2.animPla[0].interp == KeyInterp::Step && m2.animPla[1].positions.size() == cube.vertices.size());
        if (m2.animPla.size() == 2) CHECK(std::fabs(m2.animPla[1].positions[0].x - big.vertices[0].position.x) < 1e-4f);
        const SceneNode& s2 = *back.children()[3];
        CHECK(s2.spline.present && s2.spline.kind == 6 && std::fabs(s2.spline.a - 333.0f) < 1e-3f);
        const SceneNode& f2 = *back.children()[4];
        CHECK(f2.spline.points.size() == 3 && f2.spline.points[2] == glm::vec3(4, 5, 6));
        CHECK(f2.spline.extra.size() == 1 && f2.spline.extra[0].size() == 2 && f2.spline.extraClosed.size() == 1 && f2.spline.extraClosed[0]);
        CHECK(f2.spline.text == f->spline.text);
    } else {
        CHECK(false);
    }
    std::printf(g_fail ? "anim_test: %d fallos\n" : "anim_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
