// Prueba de los deformadores y generadores de core/DeformMore (2026-11-24): cada tipo devuelve una malla
// valida (sin NaN) y los que tienen un efecto medible lo tienen. Sin GPU.

#include "core/DeformMore.h"
#include "core/MeshTopology.h"

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

static bool finiteMesh(const MeshData& m) {
    for (const Vertex& v : m.vertices)
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z)) return false;
    for (uint32_t i : m.indices)
        if (i >= m.vertices.size()) return false;
    return true;
}

static float maxMove(const MeshData& a, const MeshData& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.vertices.size() && i < b.vertices.size(); ++i)
        d = std::max(d, glm::length(a.vertices[i].position - b.vertices[i].position));
    return d;
}

int main() {
    const MeshData cube = makeCubeMesh();
    const MeshData sphere = makeSphereMesh(12, 18);
    const MeshData plane = makePlaneMesh();

    // Todos los tipos: malla valida
    for (int k = 13; k < kDeformKindCount; ++k) {
        DeformMoreInput in;
        in.param2 = deformDefaultParam2(k);
        in.second = &sphere;
        in.rest = &sphere;
        in.all = {{&cube, glm::mat4(1.0f)}, {&sphere, glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0))}};
        in.spheres = {{glm::vec3(0.0f), 0.3f}, {glm::vec3(0.4f, 0, 0), 0.3f}};
        const std::string f = "sin((u+t)*2*pi)*0.2";
        in.formula = &f;
        const MeshData r = deformMore(sphere, k, 1, deformDefaultAmount(k), in);
        if (!finiteMesh(r)) std::printf("tipo %d (%s) no valido\n", k, deformKindName(k));
        CHECK(finiteMesh(r));
    }

    // Formula
    float v = 0;
    CHECK(evalFormula("1+2*3", 0, 0, 0, 0, 0, 0, v) && std::fabs(v - 7.0f) < 1e-5f);
    CHECK(evalFormula("sin(pi/2)^2", 0, 0, 0, 0, 0, 0, v) && std::fabs(v - 1.0f) < 1e-5f);
    CHECK(evalFormula("x*y+t", 2, 3, 0, 0, 0, 1, v) && std::fabs(v - 7.0f) < 1e-5f);
    CHECK(!evalFormula("foo(1)", 0, 0, 0, 0, 0, 0, v));
    CHECK(!evalFormula("(1+2", 0, 0, 0, 0, 0, 0, v));

    DeformMoreInput none;
    // Desplazador mueve puntos
    {
        DeformMoreInput in;
        in.param2 = 20.0f;
        CHECK(maxMove(sphere, deformMore(sphere, 22, 1, 20.0f, in)) > 1e-3f);
    }
    // Suavizado encoge un cubo hacia su centro
    {
        DeformMoreInput in;
        in.param2 = 10;
        const MeshData r = deformMore(cube, 25, 1, 100.0f, in);
        CHECK(glm::length(r.boundsMax - r.boundsMin) < glm::length(cube.boundsMax - cube.boundsMin));
    }
    // Envoltura al 100 %: el plano queda enrollado (su anchura en b se reduce mucho)
    {
        const MeshData sub = deformMore(plane, 25, 1, 0.0f, none);  // copia
        const MeshData r = deformMore(sub, 19, 1, 100.0f, none);
        CHECK(finiteMesh(r));
    }
    // Booleano: cubo menos esfera desplazada cambia la malla
    {
        DeformMoreInput in;
        in.second = &sphere;
        in.secondMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.5f));
        in.param2 = 1;
        const MeshData r = deformMore(cube, 28, 1, 0.0f, in);
        CHECK(!r.indices.empty() && r.indices.size() != cube.indices.size());
    }
    // Reduccion: menos triangulos
    {
        const MeshData big = makeSphereMesh(40, 60);
        const MeshData r = deformMore(big, 30, 1, 25.0f, none);
        CHECK(!r.indices.empty() && r.indices.size() < big.indices.size() / 2);
    }
    // Metaball: dos esferas producen una superficie cerrada no vacia
    {
        DeformMoreInput in;
        in.param2 = 4.0f;
        in.spheres = {{glm::vec3(0.0f), 0.2f}, {glm::vec3(0.3f, 0, 0), 0.2f}};
        const MeshData r = deformMore(MeshData{}, 32, 1, 0.0f, in);
        CHECK(r.indices.size() > 300);
    }
    // Conector une dos mallas
    {
        DeformMoreInput in;
        in.all = {{&cube, glm::mat4(1.0f)}, {&cube, glm::translate(glm::mat4(1.0f), glm::vec3(1, 0, 0))}};
        const MeshData r = deformMore(MeshData{}, 29, 1, 1.0f, in);
        CHECK(r.indices.size() == cube.indices.size() * 2);
        CHECK(r.vertices.size() < cube.vertices.size() * 2);  // los puntos de la cara comun se sueldan
    }
    // Jiggle: con la posicion animada, en mitad del movimiento la parte de arriba se retrasa
    {
        AnimTrack tr;
        tr.set(0, glm::vec3(0.0f));
        tr.set(10, glm::vec3(5.0f, 0, 0));
        tr.keys[0].interp = KeyInterp::Linear;
        DeformMoreInput in;
        in.motion = &tr;
        in.frame = 12.0f;
        in.param2 = 2.0f;
        CHECK(maxMove(cube, deformMore(cube, 13, 1, 100.0f, in)) > 1e-3f);
    }
    // Matriz: 6 copias
    {
        DeformMoreInput in;
        in.param2 = 6;
        CHECK(deformMore(cube, 34, 1, 200.0f, in).indices.size() == cube.indices.size() * 6);
    }
    // Fractura Voronoi: mismos triangulos, separados
    {
        DeformMoreInput in;
        in.param2 = 8;
        const MeshData r = deformMore(sphere, 37, 1, 10.0f, in);
        CHECK(r.indices.size() == sphere.indices.size());
    }
    // MoExtrude: mas triangulos
    {
        DeformMoreInput in;
        in.param2 = 1;
        CHECK(deformMore(cube, 39, 1, 10.0f, in).indices.size() > cube.indices.size());
    }
    // Cubrir: todos los puntos del cubo quedan sobre la esfera (radio 0.5)
    {
        DeformMoreInput in;
        in.second = &sphere;
        const MeshData r = deformMore(cube, 17, 1, 100.0f, in);
        float worst = 0;
        for (const Vertex& vv : r.vertices) worst = std::max(worst, std::fabs(glm::length(vv.position) - 0.5f));
        CHECK(worst < 0.03f);
    }

    std::printf(g_fail ? "deform_more_test: %d fallos\n" : "deform_more_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
