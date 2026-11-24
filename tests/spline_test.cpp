// Tests de splines y generadores (2026-11-24).
#include "core/DeformMore.h"
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

static bool validMesh(const MeshData& m) {
    if (m.indices.size() % 3) return false;
    for (uint32_t i : m.indices)
        if (i >= m.vertices.size()) return false;
    for (const Vertex& v : m.vertices)
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z)) return false;
    return true;
}

static float meshArea(const MeshData& m) {
    float a = 0;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const glm::vec3 p0 = m.vertices[m.indices[t]].position, p1 = m.vertices[m.indices[t + 1]].position,
                        p2 = m.vertices[m.indices[t + 2]].position;
        a += 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
    }
    return a;
}

int main() {
    // Todas las primitivas dan contornos validos (salvo Texto/Vectorizador, que dependen de fuente/imagen).
    for (int k = 0; k < kSplineKindCount; ++k) {
        SplineData s = defaultSpline(k);
        if (k == 0) s.points = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const auto cs = splineContours(s);
        if (k == 15) continue;
        if (k == 7 && cs.empty()) { std::printf("aviso: sin fuente del sistema para Texto\n"); continue; }
        CHECK(!cs.empty());
        for (const auto& c : cs) {
            CHECK(c.pts.size() >= 2);
            for (const glm::vec3& p : c.pts) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        }
    }
    // Circulo de 200 cm (1 unidad): perimetro ~ 2*pi
    {
        SplineData s = defaultSpline(2);
        const auto cs = splineContours(s);
        CHECK(cs.size() == 1 && cs[0].closed);
        CHECK(std::fabs(contourLength(cs[0]) - 6.2832f) < 0.05f);
        // tapa: area ~ pi
        const MeshData cap = capMesh(cs);
        CHECK(validMesh(cap));
        CHECK(std::fabs(meshArea(cap) - 3.1416f) < 0.05f);
    }
    // Rectangulo con agujero: area = 4 - 1
    {
        SplineContour outer{{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}, true};
        SplineContour hole{{{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}}, true};
        const MeshData cap = capMesh({outer, hole});
        CHECK(validMesh(cap));
        CHECK(std::fabs(meshArea(cap) - 3.0f) < 1e-3f);
        // extrusion: 2 tapas de 3 + paredes 8*0.5 + 4*0.5
        const MeshData ex = extrudeContours({outer, hole}, glm::vec3(0, 0, 0.5f), 1, true);
        CHECK(validMesh(ex));
        CHECK(std::fabs(meshArea(ex) - (6.0f + 4.0f + 2.0f)) < 1e-2f);
    }
    // Torno de una linea vertical a x=1 de altura 1 -> cilindro de area ~ 2*pi
    {
        SplineContour prof{{{1, 0, 0}, {1, 1, 0}}, false};
        const MeshData m = latheContour(prof, 360.0f, 64);
        CHECK(validMesh(m));
        CHECK(std::fabs(meshArea(m) - 6.2832f) < 0.05f);
    }
    // Forro entre dos circulos -> cilindro
    {
        SplineData s = defaultSpline(2);
        s.plane = 2;
        auto a = splineContours(s)[0];
        auto b = a;
        for (glm::vec3& p : b.pts) p.y += 1.0f;
        const MeshData m = loftContours({a, b}, 4, false);
        CHECK(validMesh(m));
        CHECK(std::fabs(meshArea(m) - 6.2832f) < 0.1f);
    }
    // Recorrido de un circulo r=0.1 por una recta de longitud 2 -> area ~ 2*pi*0.1*2
    {
        SplineContour prof;
        prof.closed = true;
        for (int i = 0; i < 64; ++i) prof.pts.push_back({0.1f * std::cos(i * 6.2832f / 64), 0.1f * std::sin(i * 6.2832f / 64), 0});
        SplineContour path{{{0, 0, 0}, {0, 0, 1}, {0, 0, 2}}, false};
        const MeshData m = sweepContour(prof, path, 1.0f, 0.0f, false);
        CHECK(validMesh(m));
        CHECK(std::fabs(meshArea(m) - 1.2566f) < 0.02f);
    }
    // Booleana: union de dos cuadrados solapados -> 1 contorno; interseccion -> 1 contorno de area ~ 1
    {
        SplineContour A{{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, true};
        SplineContour B{{{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}}, true};
        const auto u = splineBoolean({{A}, {B}}, 0, 256);
        CHECK(u.size() == 1);
        const auto in = splineBoolean({{A}, {B}}, 2, 256);
        CHECK(in.size() == 1);
        if (in.size() == 1) CHECK(std::fabs(meshArea(capMesh(in)) - 1.0f) < 0.05f);
        const auto sub = splineBoolean({{A}, {B}}, 1, 256);
        CHECK(sub.size() == 1);
        if (sub.size() == 1) CHECK(std::fabs(meshArea(capMesh(sub)) - 3.0f) < 0.1f);
    }
    // Texto: "O" tiene agujero -> la tapa tiene menos area que la del contorno exterior solo
    {
        const auto cs = textContours("O", 1.0f);
        if (!cs.empty()) {
            CHECK(cs.size() >= 2);
            const MeshData m = extrudeContours(cs, glm::vec3(0, 0, -0.1f), 1, true);
            CHECK(validMesh(m) && !m.indices.empty());
        }
    }
    // Generadores via deformMore
    {
        DeformMoreInput in;
        SplineData s = defaultSpline(5);
        in.splines = {splineContours(s)};
        in.splinesLocal = in.splines;
        in.param2 = 1;
        const MeshData ex = deformMore(MeshData{}, 41, 2, 40.0f, in);
        CHECK(validMesh(ex) && !ex.indices.empty());
        in.param2 = 24;
        SplineContour prof{{{0.5f, 0, 0}, {0.6f, 0.5f, 0}, {0.3f, 1, 0}}, false};
        in.splines = {{prof}};
        const MeshData lt = deformMore(MeshData{}, 42, 1, 360.0f, in);
        CHECK(validMesh(lt) && !lt.indices.empty());
        std::string txt = "Hola";
        in.formula = &txt;
        in.param2 = 20;
        const MeshData mt = deformMore(MeshData{}, 45, 1, 100.0f, in);
        CHECK(validMesh(mt));
        std::vector<SplineContour> outS;
        in.splineOut = &outS;
        in.splines = {{prof}};
        deformMore(MeshData{}, 47, 1, 50.0f, in);
        CHECK(outS.size() == 1 && contourLength(outS[0]) < contourLength(prof) * 0.6f);
        outS.clear();
        in.param2 = 12;
        const MeshData bz = deformMore(MeshData{}, 51, 1, 100.0f, in);
        CHECK(validMesh(bz) && bz.vertices.size() == 13 * 13);
    }
    // Envolver Spline: una caja larga en X se dobla sobre un arco
    {
        MeshData box;
        for (int i = 0; i <= 10; ++i)
            for (int j = 0; j < 2; ++j) box.vertices.push_back({glm::vec3(i * 0.1f - 0.5f, j * 0.1f, 0), glm::vec3(0.8f), glm::vec3(0, 0, 1), glm::vec2(0)});
        for (int i = 0; i < 10; ++i) {
            const uint32_t a = (uint32_t)(i * 2);
            box.indices.insert(box.indices.end(), {a, a + 2, a + 3, a, a + 3, a + 1});
        }
        updateBounds(box);
        DeformMoreInput in;
        SplineData arc = defaultSpline(1);
        arc.endDeg = 180;
        in.splines = {splineContours(arc)};
        const MeshData w = deformMore(box, 50, 0, 100.0f, in);
        CHECK(validMesh(w));
        CHECK(std::fabs(w.vertices[0].position.y - box.vertices[0].position.y) > 1e-3f ||
              std::fabs(w.vertices[0].position.x - box.vertices[0].position.x) > 1e-3f);
    }
    std::printf(g_fail ? "spline_test: %d fallos\n" : "spline_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
