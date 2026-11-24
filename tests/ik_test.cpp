// Prueba del solver IK (core/IkSolver): longitudes de hueso, alcance, polo, etiqueta IK y rutas con
// hermanos del mismo nombre. Sin GPU.

#include "core/IkSolver.h"

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

static glm::vec3 wp(const SceneNode& n) { return glm::vec3(n.worldMatrix()[3]); }
static bool near(const glm::vec3& a, const glm::vec3& b, float e = 5e-3f) { return glm::length(a - b) <= e; }

// Cadena J0 (origen) -> J1 (+10 en Y) -> J2 (+10 en Y).
static void makeChain(SceneNode& root, SceneNode*& j0, SceneNode*& j1, SceneNode*& j2) {
    j0 = root.addChild("J0");
    j1 = j0->addChild("J1");
    j2 = j1->addChild("J2");
    j0->isJoint = j1->isJoint = j2->isJoint = true;
    j1->position = glm::vec3(0, 10, 0);
    j2->position = glm::vec3(0, 10, 0);
}

int main() {
    {
        SceneNode root("raiz");
        SceneNode *j0, *j1, *j2;
        makeChain(root, j0, j1, j2);
        const glm::vec3 target(10, 10, 0);
        const IkResult r = solveIkChain({j0, j1, j2}, target);
        CHECK(r.solved);
        CHECK(near(wp(*j2), target));
        CHECK(near(wp(*j0), glm::vec3(0)));
        CHECK(std::abs(glm::length(wp(*j1) - wp(*j0)) - 10.0f) < 1e-2f);
        CHECK(std::abs(glm::length(wp(*j2) - wp(*j1)) - 10.0f) < 1e-2f);
    }
    {
        // Fuera de alcance: cadena recta hacia el objetivo, hueso a hueso de 10.
        SceneNode root("raiz");
        SceneNode *j0, *j1, *j2;
        makeChain(root, j0, j1, j2);
        const IkResult r = solveIkChain({j0, j1, j2}, glm::vec3(60, 0, 0));
        CHECK(!r.solved);
        CHECK(near(wp(*j2), glm::vec3(20, 0, 0)));
        CHECK(near(wp(*j1), glm::vec3(10, 0, 0)));
    }
    {
        // Polo: el codo se dobla hacia +Z.
        SceneNode root("raiz");
        SceneNode *j0, *j1, *j2;
        makeChain(root, j0, j1, j2);
        const IkResult r = solveIkChain({j0, j1, j2}, glm::vec3(0, 14, 0), glm::vec3(0, 10, 50));
        CHECK(r.solved);
        CHECK(wp(*j1).z > 1.0f);
        CHECK(std::abs(glm::length(wp(*j1) - wp(*j0)) - 10.0f) < 1e-2f);
        SceneNode root2("raiz2");
        SceneNode *k0, *k1, *k2;
        makeChain(root2, k0, k1, k2);
        solveIkChain({k0, k1, k2}, glm::vec3(0, 14, 0), glm::vec3(0, 10, -50));
        CHECK(wp(*k1).z < -1.0f);
    }
    {
        // Cadena con la raiz girada y desplazada (la jerarquia compone matrices).
        SceneNode root("raiz");
        SceneNode* base = root.addChild("Base");
        base->position = glm::vec3(5, 2, -3);
        base->rotationDeg = glm::vec3(0, 40, 25);
        SceneNode *j0, *j1, *j2;
        makeChain(*base, j0, j1, j2);
        const glm::vec3 target = wp(*j0) + glm::vec3(8, 6, 4);
        CHECK(solveIkChain({j0, j1, j2}, target).solved);
        CHECK(near(wp(*j2), target));
    }
    {
        // Etiqueta IK del tipo C4D: rutas por nombre, un objetivo que se mueve.
        SceneNode root("raiz");
        SceneNode *j0, *j1, *j2;
        makeChain(root, j0, j1, j2);
        SceneNode* goal = root.addChild("Control");
        goal->position = glm::vec3(12, 8, 0);
        j0->ik.present = true;
        j0->ik.tip = "J0/J1/J2";
        j0->ik.target = "Control";
        CHECK(solveAllIk(root) == 1);
        CHECK(near(wp(*j2), wp(*goal)));
        goal->position = glm::vec3(-6, 12, 5);
        CHECK(solveAllIk(root) == 1);
        CHECK(near(wp(*j2), wp(*goal)));
        j0->ik.enabled = false;
        goal->position = glm::vec3(0, 5, 0);
        CHECK(solveAllIk(root) == 0);
        CHECK(findNodeByPath(root, "J0/J1") == j1);
        CHECK(findNodeByPath(root, "J0/nada") == nullptr);
        CHECK(chainBetween(*j2, *j0).empty());
    }
    {
        // Hermanos con el mismo nombre (rigs de C4D): "~#2" apunta al segundo, y la IK actua sobre el correcto.
        SceneNode root("raiz");
        SceneNode* r = root.addChild("Raiz");
        SceneNode* a = r->addChild("Hueso");
        SceneNode* b = r->addChild("Hueso");
        SceneNode* c = r->addChild("Hueso");
        SceneNode* aTip = a->addChild("Punta");
        SceneNode* bTip = b->addChild("Punta");
        aTip->position = bTip->position = glm::vec3(0, 10, 0);
        a->isJoint = b->isJoint = true;
        CHECK(pathSegmentOf(*a) == "Hueso");
        CHECK(pathSegmentOf(*b) == "Hueso~#2");
        CHECK(pathSegmentOf(*c) == "Hueso~#3");
        CHECK(findNodeByPath(root, "Raiz/Hueso") == a);
        CHECK(findNodeByPath(root, "Raiz/Hueso~#2") == b);
        CHECK(findNodeByPath(root, "Raiz/Hueso~#3") == c);
        CHECK(findNodeByPath(root, "Raiz/Hueso~#4") == nullptr);
        CHECK(findNodeByPath(root, "Raiz/Hueso~#2/Punta") == bTip);
        SceneNode* goal = root.addChild("Control");
        goal->position = glm::vec3(10, 0, 0);
        b->ik.present = true;
        b->ik.tip = "Raiz/Hueso~#2/Punta";
        b->ik.target = "Control";
        CHECK(solveAllIk(root) == 1);
        CHECK(near(wp(*bTip), wp(*goal)));
        CHECK(near(wp(*aTip), glm::vec3(0, 10, 0))); // el primero no se toca
        // Un nombre que de verdad acaba en "~#2" sigue encontrandose por su nombre exacto.
        SceneNode* raro = root.addChild("Raro~#2");
        CHECK(findNodeByPath(root, "Raro~#2") == raro);
    }
    {
        // Limites de rotacion: H entre -10 y 10 grados, P libre.
        SceneNode root("raiz");
        SceneNode* j = root.addChild("J");
        j->isJoint = true;
        j->rotationDeg = glm::vec3(40.0f, 35.0f, 0.0f); // P = 40, H = 35
        j->jointLimits.present = true;
        j->jointLimits.use[0] = true; // H
        j->jointLimits.minDeg = glm::vec3(-10.0f, 0.0f, 0.0f);
        j->jointLimits.maxDeg = glm::vec3(10.0f, 0.0f, 0.0f);
        CHECK(applyJointLimits(root) == 1);
        CHECK(std::abs(j->rotationDeg.y - 10.0f) < 1e-4f);
        CHECK(std::abs(j->rotationDeg.x - 40.0f) < 1e-4f);
        CHECK(applyJointLimits(root) == 0);
        SceneNode* dup = root.addChild("Copia");
        dup->jointLimits = j->jointLimits;
        CHECK(dup->jointLimits == j->jointLimits);
    }
    if (g_fail == 0) std::printf("ik: todo correcto\n");
    return g_fail == 0 ? 0 : 1;
}
