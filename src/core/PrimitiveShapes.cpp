// Primitivas poligonales que faltaban (2026-11-20): Cilindro, Cono, Disco, Toroide, Capsula, Tubo,
// Piramide y Platonicos. Mallas editables con poligonos explicitos (faceTris): cuadrilateros en los
// lados y n-gonos en las tapas, como en C4D. Unidad = 1 (200 cm), igual que cubo y esfera.
// Cada poligono se orienta hacia fuera segun las normales que se le dan, asi que los generadores no
// tienen que preocuparse del sentido del recorrido.

#include "MeshData.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace iron3d {

namespace {

constexpr float kPi = glm::pi<float>();

struct Builder {
    MeshData m;

    uint32_t vert(const glm::vec3& p, const glm::vec3& n, glm::vec2 uv = glm::vec2(0.0f)) {
        m.vertices.push_back({p, glm::vec3(0.8f), n, uv});
        return (uint32_t)(m.vertices.size() - 1);
    }

    // Poligono convexo; se invierte si su normal de recorrido mira contra las normales de sus vertices.
    void poly(std::vector<uint32_t> ids) {
        if (ids.size() < 3) return;
        glm::vec3 nw(0.0f), avg(0.0f);
        for (size_t i = 0; i < ids.size(); ++i) {
            const glm::vec3& a = m.vertices[ids[i]].position;
            const glm::vec3& b = m.vertices[ids[(i + 1) % ids.size()]].position;
            nw.x += (a.y - b.y) * (a.z + b.z);
            nw.y += (a.z - b.z) * (a.x + b.x);
            nw.z += (a.x - b.x) * (a.y + b.y);
            avg += m.vertices[ids[i]].normal;
        }
        if (glm::dot(nw, avg) < 0.0f) std::reverse(ids.begin(), ids.end());
        for (size_t i = 1; i + 1 < ids.size(); ++i) m.indices.insert(m.indices.end(), {ids[0], ids[i], ids[i + 1]});
        m.faceTris.push_back((uint32_t)(ids.size() - 2));
    }

    // Tapa plana: n-gono con una normal comun y UV planares.
    void cap(const std::vector<glm::vec3>& pts, const glm::vec3& n, float uvScale) {
        std::vector<uint32_t> ids;
        for (const glm::vec3& p : pts) ids.push_back(vert(p, n, glm::vec2(0.5f + p.x * uvScale, 0.5f - p.z * uvScale)));
        poly(ids);
    }

    MeshData finish() {
        updateBounds(m);
        return std::move(m);
    }
};

struct ProfilePoint {
    float r, y;     // radio y altura
    float nr, ny;   // normal en el plano (radio, altura)
};

// Superficie de revolucion alrededor de Y: filas del perfil x segmentos, con costura duplicada para UV.
void lathe(Builder& b, const std::vector<ProfilePoint>& prof, int seg) {
    const int rows = (int)prof.size();
    std::vector<uint32_t> id((size_t)rows * (size_t)(seg + 1));
    for (int i = 0; i < rows; ++i)
        for (int s = 0; s <= seg; ++s) {
            const float th = 2.0f * kPi * (float)s / (float)seg;
            const float c = std::cos(th), sn = std::sin(th);
            const ProfilePoint& p = prof[(size_t)i];
            id[(size_t)i * (size_t)(seg + 1) + (size_t)s] =
                b.vert(glm::vec3(p.r * c, p.y, p.r * sn), glm::normalize(glm::vec3(p.nr * c, p.ny, p.nr * sn)),
                       glm::vec2((float)s / (float)seg, (float)i / (float)std::max(1, rows - 1)));
        }
    auto at = [&](int i, int s) { return id[(size_t)i * (size_t)(seg + 1) + (size_t)s]; };
    for (int i = 0; i + 1 < rows; ++i)
        for (int s = 0; s < seg; ++s) {
            const bool top0 = prof[(size_t)i].r < 1e-6f, top1 = prof[(size_t)i + 1].r < 1e-6f;
            if (top0 && top1) continue;
            if (top0) b.poly({at(i, s), at(i + 1, s), at(i + 1, s + 1)});
            else if (top1) b.poly({at(i, s), at(i, s + 1), at(i + 1, s)});
            else b.poly({at(i, s), at(i, s + 1), at(i + 1, s + 1), at(i + 1, s)});
        }
}

std::vector<glm::vec3> ring(float r, float y, int seg) {
    std::vector<glm::vec3> out;
    for (int s = 0; s < seg; ++s) {
        const float th = 2.0f * kPi * (float)s / (float)seg;
        out.emplace_back(r * std::cos(th), y, r * std::sin(th));
    }
    return out;
}

// Caras planas a partir de una lista de poligonos dados por indices a `pts` (platonicos, piramide).
void flatFaces(Builder& b, const std::vector<glm::vec3>& pts, const std::vector<std::vector<int>>& faces) {
    for (const auto& f : faces) {
        glm::vec3 c(0.0f);
        for (int i : f) c += pts[(size_t)i];
        c /= (float)f.size();
        glm::vec3 n(0.0f);
        for (size_t i = 0; i < f.size(); ++i) {
            const glm::vec3& a = pts[(size_t)f[i]];
            const glm::vec3& q = pts[(size_t)f[(i + 1) % f.size()]];
            n.x += (a.y - q.y) * (a.z + q.z);
            n.y += (a.z - q.z) * (a.x + q.x);
            n.z += (a.x - q.x) * (a.y + q.y);
        }
        // hacia fuera del centro de la figura (esta en el origen)
        n = glm::normalize(n);
        if (glm::dot(n, c) < 0.0f) n = -n;
        std::vector<uint32_t> ids;
        for (size_t k = 0; k < f.size(); ++k) {
            const float a = 2.0f * kPi * (float)k / (float)f.size();
            ids.push_back(b.vert(pts[(size_t)f[k]], n, glm::vec2(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a))));
        }
        b.poly(ids);
    }
}

}  // namespace

MeshData makeCylinderMesh(float radius, float height, int seg) {
    Builder b;
    const float h = height * 0.5f;
    lathe(b, {{radius, -h, 1, 0}, {radius, h, 1, 0}}, seg);
    b.cap(ring(radius, h, seg), glm::vec3(0, 1, 0), 0.5f / radius);
    b.cap(ring(radius, -h, seg), glm::vec3(0, -1, 0), 0.5f / radius);
    return b.finish();
}

MeshData makeConeMesh(float radius, float height, int seg) {
    Builder b;
    const float h = height * 0.5f;
    const float k = 1.0f / std::sqrt(height * height + radius * radius);  // normal de la pared
    lathe(b, {{radius, -h, height * k, radius * k}, {0.0f, h, height * k, radius * k}}, seg);
    b.cap(ring(radius, -h, seg), glm::vec3(0, -1, 0), 0.5f / radius);
    return b.finish();
}

MeshData makeDiscMesh(float outer, float inner, int seg) {
    Builder b;
    std::vector<ProfilePoint> prof;
    if (inner < 1e-6f) prof = {{0.0f, 0.0f, 0, 1}, {outer, 0.0f, 0, 1}};
    else prof = {{inner, 0.0f, 0, 1}, {outer, 0.0f, 0, 1}};
    lathe(b, prof, seg);
    return b.finish();
}

MeshData makeTorusMesh(float ringRadius, float pipeRadius, int ringSeg, int pipeSeg) {
    Builder b;
    std::vector<uint32_t> id((size_t)(ringSeg + 1) * (size_t)(pipeSeg + 1));
    for (int i = 0; i <= ringSeg; ++i)
        for (int j = 0; j <= pipeSeg; ++j) {
            const float u = 2.0f * kPi * (float)i / (float)ringSeg, v = 2.0f * kPi * (float)j / (float)pipeSeg;
            const glm::vec3 n(std::cos(v) * std::cos(u), std::sin(v), std::cos(v) * std::sin(u));
            const glm::vec3 c(ringRadius * std::cos(u), 0.0f, ringRadius * std::sin(u));
            id[(size_t)i * (size_t)(pipeSeg + 1) + (size_t)j] =
                b.vert(c + n * pipeRadius, n, glm::vec2((float)i / (float)ringSeg, (float)j / (float)pipeSeg));
        }
    auto at = [&](int i, int j) { return id[(size_t)i * (size_t)(pipeSeg + 1) + (size_t)j]; };
    for (int i = 0; i < ringSeg; ++i)
        for (int j = 0; j < pipeSeg; ++j) b.poly({at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
    return b.finish();
}

MeshData makeCapsuleMesh(float radius, float height, int seg, int capRings) {
    Builder b;
    const float h = std::max(0.0f, height * 0.5f - radius);  // mitad del tramo recto
    std::vector<ProfilePoint> prof;
    for (int i = 0; i <= capRings; ++i) {  // casquete inferior: del polo al ecuador
        const float a = 0.5f * kPi * (float)i / (float)capRings;
        prof.push_back({radius * std::sin(a), -h - radius * std::cos(a), std::sin(a), -std::cos(a)});
    }
    for (int i = 0; i <= capRings; ++i) {  // casquete superior: del ecuador al polo
        const float a = 0.5f * kPi * (float)i / (float)capRings;
        prof.push_back({radius * std::cos(a), h + radius * std::sin(a), std::cos(a), std::sin(a)});
    }
    lathe(b, prof, seg);
    return b.finish();
}

MeshData makeTubeMesh(float inner, float outer, float height, int seg) {
    Builder b;
    const float h = height * 0.5f;
    lathe(b, {{outer, -h, 1, 0}, {outer, h, 1, 0}}, seg);   // pared exterior
    lathe(b, {{inner, h, -1, 0}, {inner, -h, -1, 0}}, seg);  // pared interior (normal hacia el eje)
    lathe(b, {{inner, h, 0, 1}, {outer, h, 0, 1}}, seg);     // corona de arriba
    lathe(b, {{inner, -h, 0, -1}, {outer, -h, 0, -1}}, seg); // corona de abajo
    return b.finish();
}

MeshData makePyramidMesh(float width, float depth, float height) {
    Builder b;
    const float w = width * 0.5f, d = depth * 0.5f, h = height * 0.5f;
    const std::vector<glm::vec3> p = {{-w, -h, -d}, {w, -h, -d}, {w, -h, d}, {-w, -h, d}, {0.0f, h, 0.0f}};
    flatFaces(b, p, {{0, 1, 2, 3}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}});
    return b.finish();
}

MeshData makePlatonicMesh(int kind, float radius) {
    Builder b;
    std::vector<glm::vec3> p;
    std::vector<std::vector<int>> f;
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    switch (kind) {
        case 1:  // octaedro
            p = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            f = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {0, 4, 3}, {4, 1, 3}, {1, 5, 3}, {5, 0, 3}};
            break;
        case 2:  // icosaedro
            p = {{-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0}, {0, -1, phi}, {0, 1, phi},
                 {0, -1, -phi}, {0, 1, -phi}, {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1}};
            f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2},
                 {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5},
                 {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
            break;
        case 3: {  // dodecaedro: caras = vecinos del icosaedro (centros de sus 12 vertices)
            const MeshData ico = makePlatonicMesh(2, 1.0f);
            (void)ico;
            const float s = 1.0f / phi;
            p = {{1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1},
                 {0, s, phi}, {0, s, -phi}, {0, -s, phi}, {0, -s, -phi}, {s, phi, 0}, {s, -phi, 0}, {-s, phi, 0},
                 {-s, -phi, 0}, {phi, 0, s}, {phi, 0, -s}, {-phi, 0, s}, {-phi, 0, -s}};
            f = {{0, 8, 10, 2, 16}, {0, 16, 17, 1, 12}, {0, 12, 14, 4, 8}, {1, 17, 3, 11, 9}, {1, 9, 5, 14, 12},
                 {2, 10, 6, 15, 13}, {2, 13, 3, 17, 16}, {3, 13, 15, 7, 11}, {4, 14, 5, 19, 18}, {4, 18, 6, 10, 8},
                 {5, 9, 11, 7, 19}, {6, 18, 19, 7, 15}};
            break;
        }
        default:  // tetraedro
            p = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
            f = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
            break;
    }
    for (glm::vec3& v : p) v = glm::normalize(v) * radius;  // todos los vertices sobre la esfera de `radius`
    flatFaces(b, p, f);
    return b.finish();
}

ParamShape defaultParamShape(int kind) {
    ParamShape p;
    p.kind = kind;
    switch (kind) {
        case 1: p.a = 100; p.b = 200; p.seg1 = 24; break;                 // radio, altura
        case 2: p.a = 100; p.b = 200; p.seg1 = 24; break;
        case 3: p.a = 100; p.b = 0; p.seg1 = 32; break;                   // radio exterior, interior
        case 4: p.a = 80; p.b = 20; p.seg1 = 32; p.seg2 = 12; break;      // radio del anillo, del tubo
        case 5: p.a = 50; p.b = 200; p.seg1 = 24; p.seg2 = 6; break;      // radio, altura total
        case 6: p.a = 60; p.b = 100; p.c = 200; p.seg1 = 24; break;       // interior, exterior, altura
        case 7: p.a = 200; p.b = 200; p.c = 200; break;                   // ancho, fondo, alto
        case 8: p.a = 100; p.seg1 = 2; break;                             // radio, tipo (0..3)
        case 9: p.a = p.b = p.c = 200; p.seg1 = p.seg2 = p.seg3 = 1; break;   // tamano X/Y/Z, segmentos
        case 10: p.a = 100; p.seg1 = 24; break;                           // radio, segmentos
        case 11: p.a = 400; p.b = 400; p.seg1 = p.seg2 = 10; p.seg3 = 0; break;  // ancho, fondo, segmentos, orientacion
        case 12: p.a = 100; p.b = 200; p.c = 50; p.seg1 = 24; break;      // radio, altura, altura de la tapa
        case 13: p.a = 200; break;                                        // altura
        case 14: p.a = 800; p.b = 800; p.c = 100; p.seg1 = p.seg2 = 60; p.seg3 = 1; break;  // + semilla
        case 15: p.a = 400; p.b = 400; p.c = 50; p.seg1 = p.seg2 = 64; break;
        case 16: p.a = 200; p.b = 200; p.seg1 = 0; break;                 // 0 cuadrado, 1 triangulo
        default: break;
    }
    return p;
}

MeshData buildParamShape(const ParamShape& p) {
    const float k = 1.0f / 200.0f;
    const int s1 = std::max(3, p.seg1), s2 = std::max(3, p.seg2);
    switch (p.kind) {
        case 1: return makeCylinderMesh(p.a * k, p.b * k, s1);
        case 2: return makeConeMesh(p.a * k, p.b * k, s1);
        case 3: return makeDiscMesh(p.a * k, std::min(p.b, p.a * 0.98f) * k, s1);
        case 4: return makeTorusMesh(p.a * k, p.b * k, s1, s2);
        case 5: return makeCapsuleMesh(p.a * k, std::max(p.b, p.a * 2.0f) * k, s1, std::max(2, p.seg2));
        case 6: return makeTubeMesh(std::min(p.a, p.b * 0.98f) * k, p.b * k, p.c * k, s1);
        case 7: return makePyramidMesh(p.a * k, p.b * k, p.c * k);
        case 8: return makePlatonicMesh(std::clamp(p.seg1, 0, 3), p.a * k);
        case 9: return makeBoxMesh(p.a * k, p.b * k, p.c * k, std::max(1, p.seg1), std::max(1, p.seg2), std::max(1, p.seg3));
        case 10: {
            const int seg = std::max(4, p.seg1);
            MeshData m = makeSphereMesh(std::max(2, seg / 2), seg);
            for (Vertex& v : m.vertices) v.position *= p.a * k / 0.5f;
            updateBounds(m);
            return m;
        }
        case 11: return makeGridPlaneMesh(p.a * k, p.b * k, std::max(1, p.seg1), std::max(1, p.seg2), std::clamp(p.seg3, 0, 5));
        case 12: return makeOilTankMesh(p.a * k, p.b * k, p.c * k, s1);
        case 13: return makeFigureMesh(p.a * k);
        case 14: return makeLandscapeMesh(p.a * k, p.b * k, p.c * k, std::max(1, p.seg1), std::max(1, p.seg2), p.seg3);
        case 15: return makeReliefMesh(p.heights.get(), p.hw, p.hh, p.a * k, p.b * k, p.c * k, std::max(1, p.seg1), std::max(1, p.seg2));
        case 16: return makePolygonMesh(p.a * k, p.b * k, p.seg1 != 0);
        default: return MeshData{};
    }
}

namespace {
Vertex pv(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
    Vertex v{};
    v.position = p;
    v.color = glm::vec3(0.8f);
    v.normal = n;
    v.uv = uv;
    return v;
}
// Rejilla de cuadrilateros: origen o, ejes u y v (longitud completa), su x sv celdas, normal n.
void addGrid(MeshData& m, const glm::vec3& o, const glm::vec3& u, const glm::vec3& v, int su, int sv, const glm::vec3& n) {
    const uint32_t base = (uint32_t)m.vertices.size();
    for (int j = 0; j <= sv; ++j)
        for (int i = 0; i <= su; ++i)
            m.vertices.push_back(pv(o + u * ((float)i / su) + v * ((float)j / sv), n, {(float)i / su, (float)j / sv}));
    for (int j = 0; j < sv; ++j)
        for (int i = 0; i < su; ++i) {
            const uint32_t a = base + (uint32_t)(j * (su + 1) + i), b = a + 1, c = a + (uint32_t)(su + 1) + 1, d = a + (uint32_t)(su + 1);
            // orienta segun la normal pedida
            const glm::vec3 fn = glm::cross(m.vertices[b].position - m.vertices[a].position, m.vertices[d].position - m.vertices[a].position);
            if (glm::dot(fn, n) >= 0) m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
            else m.indices.insert(m.indices.end(), {a, c, b, a, d, c});
            m.faceTris.push_back(2);
        }
}
void smoothNormals(MeshData& m) {
    for (Vertex& v : m.vertices) v.normal = glm::vec3(0.0f);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        Vertex &a = m.vertices[m.indices[t]], &b = m.vertices[m.indices[t + 1]], &c = m.vertices[m.indices[t + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex& v : m.vertices) v.normal = glm::length(v.normal) > 1e-20f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
}
float hash2(int x, int y, int seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFF) / (float)0xFFFFFF;
}
float vnoise(float x, float y, int seed) {
    const int xi = (int)std::floor(x), yi = (int)std::floor(y);
    const float fx = x - xi, fy = y - yi;
    const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    const float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}
} // namespace

MeshData makeBoxMesh(float w, float h, float d, int sx, int sy, int sz) {
    MeshData m;
    const glm::vec3 hs(w * 0.5f, h * 0.5f, d * 0.5f);
    addGrid(m, {-hs.x, -hs.y, hs.z}, {w, 0, 0}, {0, h, 0}, sx, sy, {0, 0, 1});
    addGrid(m, {hs.x, -hs.y, -hs.z}, {-w, 0, 0}, {0, h, 0}, sx, sy, {0, 0, -1});
    addGrid(m, {hs.x, -hs.y, hs.z}, {0, 0, -d}, {0, h, 0}, sz, sy, {1, 0, 0});
    addGrid(m, {-hs.x, -hs.y, -hs.z}, {0, 0, d}, {0, h, 0}, sz, sy, {-1, 0, 0});
    addGrid(m, {-hs.x, hs.y, hs.z}, {w, 0, 0}, {0, 0, -d}, sx, sz, {0, 1, 0});
    addGrid(m, {-hs.x, -hs.y, -hs.z}, {w, 0, 0}, {0, 0, d}, sx, sz, {0, -1, 0});
    updateBounds(m);
    return m;
}

MeshData makeGridPlaneMesh(float w, float d, int sw, int sd, int orient) {
    MeshData m;
    addGrid(m, {-w * 0.5f, 0, -d * 0.5f}, {w, 0, 0}, {0, 0, d}, sw, sd, {0, 1, 0});
    if (orient != 0) {
        for (Vertex& v : m.vertices) {
            const glm::vec3 p = v.position, n = v.normal;
            auto rot = [&](const glm::vec3& q) -> glm::vec3 {
                switch (orient) {
                    case 1: return {q.x, -q.y, -q.z};
                    case 2: return {q.y, -q.x, q.z};
                    case 3: return {-q.y, q.x, q.z};
                    case 4: return {q.x, -q.z, q.y};
                    default: return {q.x, q.z, -q.y};
                }
            };
            v.position = rot(p);
            v.normal = rot(n);
        }
    }
    updateBounds(m);
    return m;
}

MeshData makeOilTankMesh(float radius, float height, float capHeight, int seg) {
    // perfil (r, y) de abajo arriba: polo, tapa elipsoidal, cilindro, tapa, polo
    capHeight = std::clamp(capHeight, 0.0f, height * 0.5f);
    std::vector<glm::vec2> prof;
    const int capRings = 6;
    const float y0 = -height * 0.5f, y1 = height * 0.5f;
    for (int i = 0; i <= capRings; ++i) {
        const float a = kPi * 0.5f * i / capRings;  // 0 = polo
        prof.push_back({radius * std::sin(a), y0 + capHeight * (1 - std::cos(a))});
    }
    for (int i = capRings; i >= 0; --i) {
        const float a = kPi * 0.5f * i / capRings;
        prof.push_back({radius * std::sin(a), y1 - capHeight * (1 - std::cos(a))});
    }
    MeshData m;
    const int rings = (int)prof.size();
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < seg; ++s) {
            const float a = 2 * kPi * s / seg;
            m.vertices.push_back(pv({prof[(size_t)r].x * std::cos(a), prof[(size_t)r].y, prof[(size_t)r].x * std::sin(a)}, {0, 1, 0},
                                    {(float)s / seg, (float)r / (rings - 1)}));
        }
    for (int r = 0; r + 1 < rings; ++r)
        for (int s = 0; s < seg; ++s) {
            const uint32_t a = (uint32_t)(r * seg + s), b = (uint32_t)(r * seg + (s + 1) % seg);
            const uint32_t c = (uint32_t)((r + 1) * seg + (s + 1) % seg), d = (uint32_t)((r + 1) * seg + s);
            m.indices.insert(m.indices.end(), {a, c, b, a, d, c});
            m.faceTris.push_back(2);
        }
    smoothNormals(m);
    updateBounds(m);
    return m;
}

MeshData makeFigureMesh(float height) {
    // figura de maniqui con cajas y una esfera, de pie sobre y = 0 (proporciones de 8 cabezas)
    MeshData m;
    const float u = height / 8.0f;
    auto box = [&](glm::vec3 c, glm::vec3 size) {
        MeshData b = makeBoxMesh(size.x, size.y, size.z, 1, 1, 1);
        const uint32_t base = (uint32_t)m.vertices.size();
        for (Vertex v : b.vertices) { v.position += c; m.vertices.push_back(v); }
        for (uint32_t i : b.indices) m.indices.push_back(base + i);
        m.faceTris.insert(m.faceTris.end(), b.faceTris.begin(), b.faceTris.end());
    };
    box({0, u * 5.5f, 0}, {u * 1.8f, u * 2.6f, u * 0.9f});           // torso
    box({0, u * 4.0f, 0}, {u * 1.6f, u * 0.8f, u * 0.85f});          // cadera
    box({0, u * 7.0f, 0}, {u * 0.4f, u * 0.5f, u * 0.4f});           // cuello
    for (int sd = -1; sd <= 1; sd += 2) {
        box({sd * u * 0.45f, u * 2.9f, 0}, {u * 0.6f, u * 1.8f, u * 0.6f});   // muslo
        box({sd * u * 0.45f, u * 1.05f, 0}, {u * 0.5f, u * 1.9f, u * 0.5f});  // pierna
        box({sd * u * 0.45f, u * 0.1f, u * 0.2f}, {u * 0.5f, u * 0.2f, u * 1.0f});  // pie
        box({sd * u * 1.15f, u * 5.9f, 0}, {u * 0.45f, u * 1.6f, u * 0.45f});      // brazo
        box({sd * u * 1.15f, u * 4.4f, 0}, {u * 0.4f, u * 1.5f, u * 0.4f});        // antebrazo
    }
    MeshData head = makeSphereMesh(8, 12);
    const uint32_t base = (uint32_t)m.vertices.size();
    for (Vertex v : head.vertices) { v.position = v.position * (u * 0.95f) + glm::vec3(0, u * 7.6f, 0); m.vertices.push_back(v); }
    for (uint32_t i : head.indices) m.indices.push_back(base + i);
    if (!head.faceTris.empty()) m.faceTris.insert(m.faceTris.end(), head.faceTris.begin(), head.faceTris.end());
    else for (size_t t = 0; t < head.indices.size() / 3; ++t) m.faceTris.push_back(1);
    updateBounds(m);
    return m;
}

MeshData makeLandscapeMesh(float w, float d, float h, int sw, int sd, int seed) {
    MeshData m = makeGridPlaneMesh(w, d, sw, sd, 0);
    for (Vertex& v : m.vertices) {
        const float x = (v.position.x / w + 0.5f) * 4.0f, z = (v.position.z / d + 0.5f) * 4.0f;
        float n = 0, amp = 0.5f, f = 1.0f;
        for (int o = 0; o < 6; ++o) { n += amp * vnoise(x * f, z * f, seed + o * 17); amp *= 0.5f; f *= 2.0f; }
        // borde que baja a 0, como el Terreno Fractal de C4D con bordes a nivel
        const float ex = 1.0f - std::pow(std::fabs(v.position.x / w * 2.0f), 4.0f), ez = 1.0f - std::pow(std::fabs(v.position.z / d * 2.0f), 4.0f);
        v.position.y = h * std::max(0.0f, n * 1.4f - 0.2f) * std::max(0.0f, ex) * std::max(0.0f, ez);
    }
    smoothNormals(m);
    updateBounds(m);
    return m;
}

MeshData makeReliefMesh(const std::vector<float>* heights, int hw, int hh, float w, float d, float h, int sw, int sd) {
    MeshData m = makeGridPlaneMesh(w, d, sw, sd, 0);
    if (heights && hw > 0 && hh > 0 && heights->size() >= (size_t)hw * hh) {
        for (Vertex& v : m.vertices) {
            const float u = std::clamp(v.position.x / w + 0.5f, 0.0f, 1.0f), t = std::clamp(v.position.z / d + 0.5f, 0.0f, 1.0f);
            const int x = std::min(hw - 1, (int)(u * (hw - 1) + 0.5f)), y = std::min(hh - 1, (int)(t * (hh - 1) + 0.5f));
            v.position.y = h * (*heights)[(size_t)y * hw + x];
        }
        smoothNormals(m);
    }
    updateBounds(m);
    return m;
}

MeshData makePolygonMesh(float w, float h, bool triangle) {
    MeshData m;
    const glm::vec3 n(0, 1, 0);
    if (triangle) {
        m.vertices = {pv({-w * 0.5f, 0, h * 0.5f}, n, {0, 0}), pv({w * 0.5f, 0, h * 0.5f}, n, {1, 0}), pv({0, 0, -h * 0.5f}, n, {0.5f, 1})};
        m.indices = {0, 1, 2};
        m.faceTris = {1};
        if (glm::dot(glm::cross(m.vertices[1].position - m.vertices[0].position, m.vertices[2].position - m.vertices[0].position), n) < 0)
            m.indices = {0, 2, 1};
    } else {
        addGrid(m, {-w * 0.5f, 0, -h * 0.5f}, {w, 0, 0}, {0, 0, h}, 1, 1, n);
    }
    updateBounds(m);
    return m;
}

}  // namespace iron3d
