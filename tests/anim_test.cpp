// Pistas de parametros, Nivel de Puntos y splines: evaluacion y guardado/carga (2026-11-24).
#include "core/Takes.h"
#include "core/Deform.h"
#include "core/Material.h"
#include "core/OrbitalCamera.h"
#include <filesystem>
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
    {
        // Vistas axonometricas y paralela (2026-11-24)
        using V = OrbitalCamera::View;
        OrbitalCamera c;
        c.setView(V::Isometric);
        const glm::mat4 vp = c.projectionMatrix(1.0f, false) * c.viewMatrix();
        // isometrica: los tres ejes unidad salen con la misma longitud en pantalla
        float len[3];
        for (int k = 0; k < 3; ++k) {
            const glm::vec4 a = vp * glm::vec4(0, 0, 0, 1), b = vp * glm::vec4(k == 0, k == 1, k == 2, 1);
            len[k] = glm::length(glm::vec2(b) - glm::vec2(a));
        }
        CHECK(std::fabs(len[0] - len[1]) < 1e-4f && std::fabs(len[1] - len[2]) < 1e-4f);
        c.setView(V::Gentleman);
        const glm::mat4 vg = c.projectionMatrix(1.0f, false) * c.viewMatrix();
        const glm::vec4 o = vg * glm::vec4(0, 0, 0, 1), back = vg * glm::vec4(0, 0, -1, 1);
        CHECK(back.x > o.x + 1e-4f && back.y > o.y + 1e-4f);   // caballera: el fondo va arriba a la derecha
        const glm::mat4 vgv = c.projectionMatrix(1.0f, true) * c.viewMatrix();   // Vulkan: Y invertida
        const glm::vec4 o2 = vgv * glm::vec4(0, 0, 0, 1), b2 = vgv * glm::vec4(0, 0, -1, 1);
        CHECK(b2.x > o2.x + 1e-4f && b2.y < o2.y - 1e-4f);
        CHECK(!c.canOrbit());
        c.setView(V::Parallel);
        CHECK(c.canOrbit() && c.isOrtho());
    }
    {
        // Archivo de materiales (.imat = escena sin objetos): ida y vuelta
        std::vector<Material> mats(2);
        mats[0].name = "Rojo";
        mats[0].baseColor = glm::vec3(1, 0, 0);
        mats[1].name = "PBR";
        mats[1].roughness = 0.25f;
        mats[1].useColor = false;
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "iron3d_anim_test.imat";
        SceneNode empty("Root");
        CHECK(saveScene(p, empty, mats));
        SceneNode back("Root");
        std::vector<Material> got;
        CHECK(loadScene(p, back, got));
        CHECK(got.size() == 2 && got[0].name == "Rojo" && got[0].baseColor.x == 1.0f && got[1].roughness == 0.25f && !got[1].useColor);
        CHECK(back.children().empty());
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }
    {
        // Basico/Phong se guardan (linea DISP)
        SceneNode root("Root");
        SceneNode* n = root.addChild("C", PrimitiveType::Cube);
        n->useColor = 2;
        n->displayColor = glm::vec3(0.1f, 0.2f, 0.3f);
        n->xray = true;
        n->phongAngle = 30.0f;
        std::vector<Material> none;
        const std::string txt = serializeScene(root, none);
        SceneNode r2("Root");
        std::vector<Material> m2;
        std::string err;
        CHECK(deserializeScene(txt, r2, m2, &err));
        if (!err.empty()) std::printf("DISP: %s\n%s\n", err.c_str(), txt.c_str());
        CHECK(!r2.children().empty());
        if (!r2.children().empty()) {
            const SceneNode& c2 = *r2.children()[0];
            CHECK(c2.useColor == 2 && c2.xray && std::fabs(c2.displayColor.z - 0.3f) < 1e-5f && std::fabs(c2.phongAngle - 30.0f) < 1e-4f);
        }
    }
    {   // Datos de Usuario y Tomas: guardar y cargar, y cambiar de toma
        SceneNode root("Root");
        SceneNode* a = root.addChild("A", PrimitiveType::Cube);
        a->position = glm::vec3(1, 0, 0);
        a->userData.push_back({"vida", 1, 7.0f, ""});
        a->userData.push_back({"nota", 3, 0.0f, "hola mundo"});
        takes::ensure(root);
        const int t1 = takes::add(root, "Toma B");
        takes::activate(root, t1);
        CHECK(takes::addOverride(root, *a));
        a->position = glm::vec3(5, 0, 0);
        a->editorVis = VisMode::Off;
        takes::activate(root, 0);
        CHECK(std::fabs(a->position.x - 1.0f) < 1e-5f && a->editorVis == VisMode::Default);
        takes::activate(root, t1);
        CHECK(std::fabs(a->position.x - 5.0f) < 1e-5f && a->editorVis == VisMode::Off);
        std::vector<Material> none;
        const std::string txt = serializeScene(root, none);
        SceneNode r2("Root");
        std::vector<Material> m2;
        std::string err;
        CHECK(deserializeScene(txt, r2, m2, &err));
        if (!err.empty()) std::printf("TAKES: %s\n", err.c_str());
        CHECK(r2.takes.size() == 2 && r2.activeTake == 1 && r2.takes[1].overrides.size() == 1);
        if (!r2.children().empty()) {
            const SceneNode& a2 = *r2.children()[0];
            CHECK(a2.userData.size() == 2 && a2.userData[0].name == "vida" && a2.userData[1].text == "hola mundo");
            takes::activate(r2, 0);
            CHECK(std::fabs(a2.position.x - 1.0f) < 1e-5f);
        }
    }
    std::printf(g_fail ? "anim_test: %d fallos\n" : "anim_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
