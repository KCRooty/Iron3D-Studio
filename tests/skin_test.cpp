// Prueba de la Piel en vivo (core/Skin): la malla sigue a las articulaciones. Sin GPU.

#include "core/Skin.h"
#include "core/NullShapes.h"
#include "core/SceneNode.h"
#include "core/Symmetry.h"

#include <cmath>
#include <cstdio>

using namespace iron3d;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #c);         \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)

static bool near(const glm::vec3& a, const glm::vec3& b, float e = 1e-3f) { return glm::length(a - b) <= e; }

int main() {
    SceneNode root("root");
    SceneNode* hueso = root.addChild("Hueso");
    hueso->isJoint = true;
    SceneNode* malla = root.addChild("Malla", PrimitiveType::Mesh);

    auto md = std::make_shared<MeshData>();
    const glm::vec3 pts[3] = {{1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    for (const glm::vec3& p : pts) {
        Vertex v{};
        v.position = p;
        v.color = glm::vec3(0.8f);
        v.normal = glm::vec3(0, 1, 0);
        md->vertices.push_back(v);
    }
    md->indices = {0, 1, 2};
    updateBounds(*md);
    malla->mesh = md;

    auto sk = std::make_shared<SkinData>();
    sk->jointPaths = {"Hueso"};
    sk->bindMesh = md;
    sk->influences.resize(3);
    sk->influences[0].joint[0] = 0; sk->influences[0].weight[0] = 1.0f;   // sigue al hueso
    sk->influences[1].joint[0] = 0; sk->influences[1].weight[0] = 0.5f;   // mitad hueso, mitad rigido
    // el tercero sin pesos: rigido
    malla->skin = sk;

    CHECK(bindSkins(root) == 1);
    CHECK(sk->bound);
    CHECK(evaluateSkins(root) == 0);        // nada se ha movido: no se rehace

    // Hueso girado 90 grados en torno a Y (H = 90): X pasa a -Z.
    hueso->rotationDeg = glm::vec3(0.0f, 90.0f, 0.0f);
    CHECK(evaluateSkins(root) == 1);
    CHECK(malla->mesh != md);
    const auto& v = malla->mesh->vertices;
    CHECK(near(v[0].position, glm::vec3(0, 0, -1)));
    CHECK(near(v[1].position, 0.5f * glm::vec3(0, 0, -2) + 0.5f * glm::vec3(2, 0, 0)));
    CHECK(near(v[2].position, glm::vec3(3, 0, 0)));
    CHECK(near(v[0].normal, glm::vec3(0, 1, 0)));   // la normal vertical no cambia al girar en Y
    CHECK(evaluateSkins(root) == 0);        // misma pose: cache

    // Mover el objeto de malla: la Piel respeta su matriz (resultado en local).
    hueso->rotationDeg = glm::vec3(0.0f);
    CHECK(evaluateSkins(root) == 1);
    CHECK(near(malla->mesh->vertices[0].position, glm::vec3(1, 0, 0)));

    // Editar la malla descarta la Piel.
    auto editada = std::make_shared<MeshData>(*malla->mesh);
    malla->mesh = editada;
    hueso->rotationDeg = glm::vec3(0.0f, 45.0f, 0.0f);
    CHECK(evaluateSkins(root) == 0);
    CHECK(!malla->skin);
    CHECK(malla->mesh == editada);

    // Una articulacion inexistente: la Piel no se vincula.
    SceneNode m2("m2", PrimitiveType::Mesh);
    auto s2 = std::make_shared<SkinData>();
    s2->jointPaths = {"Fantasma"};
    s2->bindMesh = md;
    s2->influences.resize(3);
    m2.mesh = md;
    m2.skin = s2;
    SceneNode r2("r2");
    r2.appendChildrenFrom(m2);   // no-op: m2 no tiene hijos
    SceneNode* m3 = root.addChild("m3", PrimitiveType::Mesh);
    m3->mesh = md;
    m3->skin = s2;
    CHECK(bindSkins(root) == 0);
    CHECK(!m3->skin);

    // Formas de Nulo (controladores de rig).
    SceneNode scene("escena");
    SceneNode* ctrl = scene.addChild("Control");
    ctrl->position = glm::vec3(1, 2, 3);
    ctrl->nullDisplay.shape = NullCircle;
    ctrl->nullDisplay.radius = 0.5f;
    std::vector<Vertex> lines;
    buildNullShapes(scene, nullptr, lines);
    CHECK(lines.size() == 96);                       // 48 segmentos
    for (const Vertex& vx : lines) CHECK(std::fabs(glm::length(vx.position - glm::vec3(1, 2, 3)) - 0.5f) < 1e-3f);
    ctrl->nullDisplay.shape = NullCube;
    lines.clear();
    buildNullShapes(scene, ctrl, lines);
    CHECK(lines.size() == 24);                       // 12 aristas
    ctrl->nullDisplay.shape = NullSphere;
    lines.clear();
    buildNullShapes(scene, nullptr, lines);
    CHECK(lines.size() == 288);                      // 3 circulos
    ctrl->editorVis = VisMode::Off;
    lines.clear();
    buildNullShapes(scene, nullptr, lines);
    CHECK(lines.empty());                            // oculto en el editor
    ctrl->editorVis = VisMode::Default;
    ctrl->nullDisplay.shape = NullNone;
    buildNullShapes(scene, nullptr, lines);
    CHECK(lines.empty());
    {
        const SceneNode* copy = duplicateSubtree(scene, *ctrl);
        CHECK(copy->nullDisplay == ctrl->nullDisplay);
    }

    // Seleccion por clic: el rayo toca las lineas del circulo, no su interior ni lo que queda lejos.
    {
        SceneNode ctl("Ctl");
        ctl.nullDisplay.shape = NullCircle;
        ctl.nullDisplay.radius = 0.5f;
        const glm::mat4 w(1.0f);
        float t = 0.0f;
        CHECK(rayHitsNullShape(ctl, w, glm::vec3(0.5f, 0.0f, 5.0f), glm::vec3(0, 0, -1), t));   // borde (+X)
        CHECK(std::fabs(t - 5.0f) < 0.1f);
        CHECK(!rayHitsNullShape(ctl, w, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0, 0, -1), t));  // centro vacio
        CHECK(!rayHitsNullShape(ctl, w, glm::vec3(2.0f, 0.0f, 5.0f), glm::vec3(0, 0, -1), t));  // fuera
        CHECK(!rayHitsNullShape(ctl, w, glm::vec3(0.5f, 0.0f, -5.0f), glm::vec3(0, 0, 1), t) == false);   // por detras tambien toca
        ctl.nullDisplay.shape = NullNone;
        CHECK(!rayHitsNullShape(ctl, w, glm::vec3(0.5f, 0.0f, 5.0f), glm::vec3(0, 0, -1), t));
    }

    // Objeto Simetria: refleja a su hijo respecto al plano X del nodo, sin tocar al hijo.
    {
        SceneNode r2("r2");
        SceneNode* sym = r2.addChild("Simetria");
        sym->symmetry.present = true;
        SceneNode* hijo = sym->addChild("Hijo", PrimitiveType::Mesh);
        hijo->position = glm::vec3(2.0f, 0.0f, 0.0f);
        auto hm = std::make_shared<MeshData>();
        Vertex v{};
        v.normal = glm::vec3(1, 0, 0);
        v.position = glm::vec3(0.0f, 0.0f, 0.0f); hm->vertices.push_back(v);   // en el hijo; en el padre queda en x = 2
        v.position = glm::vec3(0.0f, 1.0f, 0.0f); hm->vertices.push_back(v);
        v.position = glm::vec3(0.0f, 0.0f, 1.0f); hm->vertices.push_back(v);
        hm->indices = {0, 1, 2};
        updateBounds(*hm);
        hijo->mesh = hm;
        CHECK(evaluateSymmetry(r2) == 1);
        CHECK(sym->mesh && sym->primitive == PrimitiveType::Mesh);
        if (sym->mesh) {
            CHECK(sym->mesh->vertices.size() == 3);
            CHECK(near(sym->mesh->vertices[0].position, glm::vec3(-2.0f, 0.0f, 0.0f)));
            CHECK(near(sym->mesh->vertices[1].position, glm::vec3(-2.0f, 1.0f, 0.0f)));
            CHECK(near(sym->mesh->vertices[0].normal, glm::vec3(-1, 0, 0)));
            CHECK(sym->mesh->indices[1] == 2 && sym->mesh->indices[2] == 1);  // giro invertido
        }
        CHECK(near(hijo->mesh->vertices[0].position, glm::vec3(0.0f)));       // el hijo no cambia
        CHECK(evaluateSymmetry(r2) == 0);                                      // sin cambios: no recalcula
        hijo->position = glm::vec3(3.0f, 0.0f, 0.0f);
        CHECK(evaluateSymmetry(r2) == 1);
        CHECK(near(sym->mesh->vertices[0].position, glm::vec3(-3.0f, 0.0f, 0.0f)));
        sym->symmetry.axis = 1;                                                // plano Y
        CHECK(evaluateSymmetry(r2) == 1);
        CHECK(near(sym->mesh->vertices[1].position, glm::vec3(3.0f, -1.0f, 0.0f)));
        // Puntos sobre el plano se pegan a el (tolerancia): la mitad refleja y toca la original sin hueco.
        hijo->position = glm::vec3(0.0f);
        sym->symmetry.axis = 0;
        sym->symmetry.tolCm = 1.0f;
        hm->vertices[0].position.x = 0.001f;
        hijo->mesh = std::make_shared<MeshData>(*hm);
        evaluateSymmetry(r2);
        CHECK(sym->mesh && near(sym->mesh->vertices[0].position, glm::vec3(0.0f, 0.0f, 0.0f), 1e-6f));
    }

    if (g_fail == 0) std::printf("skin_test: todo OK\n");
    return g_fail == 0 ? 0 : 1;
}
