// Pruebas de la geometria compatible con C4D (importers/c4d_compat). Sin GPU
// ni Cineware: se ejecutan en el CI de Windows y Linux.

#include "core/SceneNode.h"
#include "importers/c4d_compat/C4DGeometry.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <functional>

using namespace iron3d;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FALLO: %s\n", what);
    }
}

// Todas las caras de la malla de render miran hacia fuera segun `outward`
// (direccion "hacia fuera" en el centro de cada triangulo).
bool facesPointOut(const MeshData& m, const std::function<glm::vec3(const glm::vec3&)>& outward) {
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3 a = m.vertices[m.indices[i]].position, b = m.vertices[m.indices[i + 1]].position,
                        c = m.vertices[m.indices[i + 2]].position;
        const glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::length(n) < 1e-12f) continue;
        if (glm::dot(n, outward((a + b + c) / 3.0f)) <= 0.0f) return false;
        // La normal de los vertices debe ir hacia el mismo lado que la cara.
        if (glm::dot(m.vertices[m.indices[i]].normal, n) <= 0.0f) return false;
    }
    return !m.indices.empty();
}

glm::vec3 fromCenter(const glm::vec3& p) { return p; }

bool nearly(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

} // namespace

int main() {
    // --- Primitivas: orientacion de caras tras el espejo de C4D -> Iron3D ----
    {
        const MeshData cube = c4d::toIronMesh(c4d::makeCube({200.0, 200.0, 200.0}), -1.0);
        check(cube.indices.size() == 36, "cubo: 12 triangulos");
        check(facesPointOut(cube, fromCenter), "cubo: caras hacia fuera");
        check(nearly(cube.boundsMin, glm::vec3(-0.5f)) && nearly(cube.boundsMax, glm::vec3(0.5f)),
              "cubo de 200 cm = 1 unidad");
    }
    {
        const MeshData s = c4d::toIronMesh(c4d::makeSphere(100.0, 24), 80.0 * 3.14159265 / 180.0);
        check(facesPointOut(s, fromCenter), "esfera: caras hacia fuera");
        check(nearly(s.boundsMax, glm::vec3(0.5f), 1e-3f), "esfera de radio 100 cm");
    }
    for (int axis = 0; axis < 6; ++axis) {
        const MeshData cyl = c4d::toIronMesh(c4d::makeCylinder(50.0, 200.0, 16, true, (c4d::Axis)axis), -1.0);
        check(facesPointOut(cyl, fromCenter), "cilindro: caras hacia fuera (todos los ejes)");
        const MeshData plane = c4d::toIronMesh(c4d::makePlane(400.0, 400.0, (c4d::Axis)axis), -1.0);
        check(plane.indices.size() == 6, "plano: 2 triangulos");
    }
    {
        // Cono: el centro del volumen no es el origen; basta con que la caja
        // sea la esperada y la base mire hacia abajo.
        const MeshData cone = c4d::toIronMesh(c4d::makeCone(0.0, 100.0, 200.0, 16, true, c4d::Axis::PosY), -1.0);
        check(nearly(cone.boundsMin, glm::vec3(-0.5f, -0.5f, -0.5f), 1e-3f), "cono: caja");
        check(facesPointOut(cone, [](const glm::vec3& p) { return p - glm::vec3(0.0f, -0.25f, 0.0f); }),
              "cono: caras hacia fuera");
    }
    {
        const MeshData t = c4d::toIronMesh(c4d::makeTorus(200.0, 50.0, 24, 12, c4d::Axis::PosY), 1.4);
        check(facesPointOut(t, [](const glm::vec3& p) {
                  glm::vec3 ring(p.x, 0.0f, p.z);
                  ring = glm::length(ring) > 1e-6f ? glm::normalize(ring) * 1.0f : ring; // 200 cm = 1 unidad
                  return p - ring;
              }),
              "toro: caras hacia fuera");
    }
    {
        const MeshData py = c4d::toIronMesh(c4d::makePyramid({200.0, 200.0, 200.0}), -1.0);
        check(facesPointOut(py, [](const glm::vec3& p) { return p - glm::vec3(0.0f, -0.25f, 0.0f); }),
              "piramide: caras hacia fuera");
        const MeshData disc = c4d::toIronMesh(c4d::makeDisc(20.0, 100.0, 16, c4d::Axis::PosY), -1.0);
        check(facesPointOut(disc, [](const glm::vec3&) { return glm::vec3(0.0f, 1.0f, 0.0f); }), "disco hacia +Y");
    }
    {
        // Malla escrita "a mano" con el orden de C4D (la piramide de la
        // prueba con Cineware): base (0,1,2,3) y 4 lados hacia la punta.
        c4d::PolySoup s;
        s.points = {{100, 0, 100}, {-100, 0, 100}, {-100, 0, -100}, {100, 0, -100}, {0, 200, 0}};
        s.polys = {{0, 1, 2, 3}, {4, 1, 0, 0}, {4, 2, 1, 1}, {4, 3, 2, 2}, {4, 0, 3, 3}};
        const MeshData m = c4d::toIronMesh(s, -1.0);
        check(facesPointOut(m, [](const glm::vec3& p) { return p - glm::vec3(0.0f, 0.25f, 0.0f); }),
              "malla de C4D: caras hacia fuera");
    }

    // --- Matriz de C4D -> TRS de SceneNode ------------------------------------
    // Invariante: convertir el punto ya transformado en C4D == aplicar el TRS
    // de Iron3D al punto convertido.
    {
        auto c4dApply = [](const glm::dvec3& off, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3,
                           const glm::dvec3& p) { return off + p.x * v1 + p.y * v2 + p.z * v3; };
        struct Case { glm::dvec3 off, v1, v2, v3; const char* name; };
        const double c = std::cos(0.7), s = std::sin(0.7), c2 = std::cos(-1.1), s2 = std::sin(-1.1);
        // Rotacion compuesta (alrededor de Y y luego X) con escala no uniforme.
        const glm::dmat3 ry(glm::dvec3(c, 0, -s), glm::dvec3(0, 1, 0), glm::dvec3(s, 0, c));
        const glm::dmat3 rx(glm::dvec3(1, 0, 0), glm::dvec3(0, c2, s2), glm::dvec3(0, -s2, c2));
        const glm::dmat3 r = rx * ry;
        const Case cases[] = {
            {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "identidad"},
            {{100, 50, 200}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "traslacion"},
            {{10, -20, 30}, r[0] * 2.0, r[1] * 0.5, r[2] * 1.5, "rotacion + escala"},
            {{0, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "espejo en X"},
        };
        for (const Case& k : cases) {
            const c4d::TRS trs = c4d::toIronTRS(k.off, k.v1, k.v2, k.v3);
            SceneNode n("t");
            n.position = trs.position;
            n.rotationDeg = trs.rotationDeg;
            n.scale = trs.scale;
            const glm::mat4 m = n.localMatrix();
            const glm::dvec3 pts[] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {0, 0, 100}, {30, -70, 45}};
            bool ok = true;
            for (const glm::dvec3& p : pts) {
                const glm::vec3 expect = c4d::toIronPoint(c4dApply(k.off, k.v1, k.v2, k.v3, p));
                const glm::vec3 got = glm::vec3(m * glm::vec4(c4d::toIronPoint(p), 1.0f));
                ok = ok && nearly(expect, got, 1e-3f);
            }
            check(ok, k.name);
        }
    }

    if (g_failures == 0) std::printf("c4d_geometry: todo correcto\n");
    return g_failures == 0 ? 0 : 1;
}
