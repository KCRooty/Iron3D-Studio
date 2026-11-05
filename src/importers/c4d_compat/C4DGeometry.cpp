#include "C4DGeometry.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace iron3d::c4d {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Normal de Newell (vale para triangulos y cuadrilateros no planos); con el
// orden de C4D sale hacia el lado frontal, como cross(b - a, c - a).
glm::dvec3 newell(const PolySoup& s, const int* idx, int n) {
    glm::dvec3 nrm(0.0);
    for (int i = 0; i < n; ++i) nrm += glm::cross(s.points[(size_t)idx[i]], s.points[(size_t)idx[(i + 1) % n]]);
    return nrm;
}

// Anade una cara y la orienta para que su frontal mire hacia `outward`.
void addFace(PolySoup& s, std::array<int, 4> f, const glm::dvec3& outward) {
    const int n = f[2] == f[3] ? 3 : 4;
    if (glm::dot(newell(s, f.data(), n), outward) < 0.0) {
        if (n == 3) {
            std::swap(f[1], f[2]);
            f[3] = f[2];
        } else {
            std::swap(f[1], f[3]);
        }
    }
    s.polys.push_back(f);
}

void addTri(PolySoup& s, int a, int b, int c, const glm::dvec3& outward) { addFace(s, {a, b, c, c}, outward); }
void addQuad(PolySoup& s, int a, int b, int c, int d, const glm::dvec3& outward) { addFace(s, {a, b, c, d}, outward); }

glm::dvec3 centroid(const PolySoup& s, std::initializer_list<int> idx) {
    glm::dvec3 c(0.0);
    for (int i : idx) c += s.points[(size_t)i];
    return c / (double)idx.size();
}

int addPoint(PolySoup& s, const glm::dvec3& p) {
    s.points.push_back(p);
    return (int)s.points.size() - 1;
}

// Gira una primitiva construida a lo largo de +Y hacia el eje pedido
// (rotaciones puras: no cambian la orientacion de las caras).
glm::dvec3 rotateToAxis(const glm::dvec3& p, Axis axis) {
    switch (axis) {
        case Axis::PosX: return {p.y, -p.x, p.z};
        case Axis::NegX: return {-p.y, p.x, p.z};
        case Axis::NegY: return {p.x, -p.y, -p.z};
        case Axis::PosZ: return {p.x, -p.z, p.y};
        case Axis::NegZ: return {p.x, p.z, -p.y};
        case Axis::PosY: default: return p;
    }
}

void applyAxis(PolySoup& s, Axis axis) {
    if (axis == Axis::PosY) return;
    for (glm::dvec3& p : s.points) p = rotateToAxis(p, axis);
}

// Superficie de revolucion alrededor de Y: anillos (radio, altura) de abajo
// arriba. Un radio 0 colapsa el anillo en un solo punto (punta de cono).
// Las caras laterales se orientan hacia fuera del eje Y.
PolySoup revolve(const std::vector<std::pair<double, double>>& rings, int segments, bool capBottom, bool capTop) {
    PolySoup s;
    segments = std::max(3, segments);
    std::vector<std::vector<int>> ringIdx;
    for (const auto& r : rings) {
        std::vector<int> idx;
        if (r.first <= 1e-9) {
            idx.assign((size_t)segments, addPoint(s, {0.0, r.second, 0.0}));
        } else {
            for (int k = 0; k < segments; ++k) {
                const double a = 2.0 * kPi * k / segments;
                idx.push_back(addPoint(s, {r.first * std::sin(a), r.second, r.first * std::cos(a)}));
            }
        }
        ringIdx.push_back(std::move(idx));
    }
    for (size_t r = 0; r + 1 < ringIdx.size(); ++r) {
        const double yMid = 0.5 * (rings[r].second + rings[r + 1].second);
        for (int k = 0; k < segments; ++k) {
            const int k1 = (k + 1) % segments;
            const int a = ringIdx[r][(size_t)k], b = ringIdx[r][(size_t)k1];
            const int c = ringIdx[r + 1][(size_t)k1], d = ringIdx[r + 1][(size_t)k];
            if (a == b && c == d) continue; // dos puntas seguidas: nada que unir
            const glm::dvec3 out = centroid(s, {a, b, c, d}) - glm::dvec3(0.0, yMid, 0.0);
            if (a == b) addTri(s, a, c, d, out);
            else if (c == d) addTri(s, a, b, c, out);
            else addQuad(s, a, b, c, d, out);
        }
    }
    auto cap = [&](size_t r, double dir) {
        if (rings[r].first <= 1e-9) return;
        const int center = addPoint(s, {0.0, rings[r].second, 0.0});
        for (int k = 0; k < segments; ++k)
            addTri(s, center, ringIdx[r][(size_t)k], ringIdx[r][(size_t)((k + 1) % segments)], {0.0, dir, 0.0});
    };
    if (capBottom) cap(0, -1.0);
    if (capTop) cap(ringIdx.size() - 1, 1.0);
    return s;
}

} // namespace

void PolySoup::append(const PolySoup& other, const glm::dmat4& xform) {
    const int base = (int)points.size();
    for (const glm::dvec3& p : other.points) points.push_back(glm::dvec3(xform * glm::dvec4(p, 1.0)));
    for (const auto& f : other.polys) polys.push_back({f[0] + base, f[1] + base, f[2] + base, f[3] + base});
}

Axis axisFromC4D(int value) {
    return value >= 0 && value <= 5 ? (Axis)value : Axis::PosY;
}

PolySoup makeCube(const glm::dvec3& size) {
    PolySoup s;
    const glm::dvec3 h = size * 0.5;
    for (int i = 0; i < 8; ++i) addPoint(s, {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z});
    // Cada cara: los 4 vertices con el bit del eje fijo, en orden de vuelta.
    const int faces[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    for (const auto& f : faces) addQuad(s, f[0], f[1], f[2], f[3], centroid(s, {f[0], f[1], f[2], f[3]}));
    return s;
}

PolySoup makeSphere(double radius, int segments) {
    segments = std::max(4, segments);
    const int rings = std::max(2, segments / 2);
    std::vector<std::pair<double, double>> prof;
    for (int r = 0; r <= rings; ++r) {
        const double t = kPi * r / rings; // 0 = polo sur
        prof.push_back({radius * std::sin(t), -radius * std::cos(t)});
    }
    PolySoup s = revolve(prof, segments, false, false);
    return s;
}

PolySoup makePlane(double width, double height, Axis axis) {
    PolySoup s;
    const double w = width * 0.5, h = height * 0.5;
    const int a = addPoint(s, {-w, 0.0, -h}), b = addPoint(s, {w, 0.0, -h});
    const int c = addPoint(s, {w, 0.0, h}), d = addPoint(s, {-w, 0.0, h});
    addQuad(s, a, b, c, d, {0.0, 1.0, 0.0});
    applyAxis(s, axis);
    return s;
}

PolySoup makeCylinder(double radius, double height, int segments, bool caps, Axis axis) {
    return makeCone(radius, radius, height, segments, caps, axis);
}

PolySoup makeCone(double topRadius, double bottomRadius, double height, int segments, bool caps, Axis axis) {
    const double h = height * 0.5;
    PolySoup s = revolve({{bottomRadius, -h}, {topRadius, h}}, segments, caps, caps);
    applyAxis(s, axis);
    return s;
}

PolySoup makeTorus(double ringRadius, double pipeRadius, int segments, int pipeSegments, Axis axis) {
    PolySoup s;
    segments = std::max(3, segments);
    pipeSegments = std::max(3, pipeSegments);
    std::vector<glm::dvec3> centers;
    for (int i = 0; i < segments; ++i) {
        const double a = 2.0 * kPi * i / segments;
        const glm::dvec3 dir(std::sin(a), 0.0, std::cos(a));
        centers.push_back(dir * ringRadius);
        for (int j = 0; j < pipeSegments; ++j) {
            const double b = 2.0 * kPi * j / pipeSegments;
            addPoint(s, dir * (ringRadius + pipeRadius * std::cos(b)) + glm::dvec3(0.0, pipeRadius * std::sin(b), 0.0));
        }
    }
    auto at = [&](int i, int j) { return (i % segments) * pipeSegments + (j % pipeSegments); };
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < pipeSegments; ++j) {
            const int a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            const glm::dvec3 ringC = 0.5 * (centers[(size_t)i] + centers[(size_t)((i + 1) % segments)]);
            addQuad(s, a, b, c, d, centroid(s, {a, b, c, d}) - ringC);
        }
    }
    applyAxis(s, axis);
    return s;
}

PolySoup makeDisc(double innerRadius, double outerRadius, int segments, Axis axis) {
    PolySoup s;
    segments = std::max(3, segments);
    const glm::dvec3 up(0.0, 1.0, 0.0);
    std::vector<int> outer, inner;
    for (int k = 0; k < segments; ++k) {
        const double a = 2.0 * kPi * k / segments;
        outer.push_back(addPoint(s, {outerRadius * std::sin(a), 0.0, outerRadius * std::cos(a)}));
    }
    if (innerRadius > 1e-9) {
        for (int k = 0; k < segments; ++k) {
            const double a = 2.0 * kPi * k / segments;
            inner.push_back(addPoint(s, {innerRadius * std::sin(a), 0.0, innerRadius * std::cos(a)}));
        }
        for (int k = 0; k < segments; ++k) {
            const int k1 = (k + 1) % segments;
            addQuad(s, inner[(size_t)k], outer[(size_t)k], outer[(size_t)k1], inner[(size_t)k1], up);
        }
    } else {
        const int c = addPoint(s, {0.0, 0.0, 0.0});
        for (int k = 0; k < segments; ++k) addTri(s, c, outer[(size_t)k], outer[(size_t)((k + 1) % segments)], up);
    }
    applyAxis(s, axis);
    return s;
}

PolySoup makePyramid(const glm::dvec3& size) {
    PolySoup s;
    const glm::dvec3 h = size * 0.5;
    const int a = addPoint(s, {-h.x, -h.y, -h.z}), b = addPoint(s, {h.x, -h.y, -h.z});
    const int c = addPoint(s, {h.x, -h.y, h.z}), d = addPoint(s, {-h.x, -h.y, h.z});
    const int top = addPoint(s, {0.0, h.y, 0.0});
    const glm::dvec3 mid(0.0, -h.y * 0.5, 0.0); // centro aproximado del volumen
    addQuad(s, a, b, c, d, {0.0, -1.0, 0.0});
    addTri(s, a, b, top, centroid(s, {a, b, top}) - mid);
    addTri(s, b, c, top, centroid(s, {b, c, top}) - mid);
    addTri(s, c, d, top, centroid(s, {c, d, top}) - mid);
    addTri(s, d, a, top, centroid(s, {d, a, top}) - mid);
    return s;
}

glm::vec3 toIronPoint(const glm::dvec3& p) {
    return glm::vec3((float)(p.x / kCmPerUnit), (float)(p.y / kCmPerUnit), (float)(-p.z / kCmPerUnit));
}

MeshData toIronMesh(const PolySoup& soup, double smoothAngleRad) {
    MeshData mesh;
    const size_t np = soup.points.size();
    std::vector<glm::vec3> pts(np);
    for (size_t i = 0; i < np; ++i) pts[i] = toIronPoint(soup.points[i]);

    // El espejo en Z invierte el sentido de giro: se invierte el orden de
    // cada cara para que la normal siga saliendo por el mismo lado.
    struct Face { int idx[4]; int n; glm::vec3 area; glm::vec3 unit; };
    std::vector<Face> faces;
    faces.reserve(soup.polys.size());
    for (const auto& p : soup.polys) {
        Face f{};
        if (p[2] == p[3]) { f.n = 3; f.idx[0] = p[0]; f.idx[1] = p[2]; f.idx[2] = p[1]; }
        else { f.n = 4; f.idx[0] = p[0]; f.idx[1] = p[3]; f.idx[2] = p[2]; f.idx[3] = p[1]; }
        bool ok = true;
        for (int k = 0; k < f.n; ++k) ok = ok && f.idx[k] >= 0 && (size_t)f.idx[k] < np;
        if (!ok) continue;
        glm::vec3 nrm(0.0f);
        for (int k = 0; k < f.n; ++k) nrm += glm::cross(pts[(size_t)f.idx[k]], pts[(size_t)f.idx[(k + 1) % f.n]]);
        f.area = nrm;
        const float len = glm::length(nrm);
        f.unit = len > 1e-20f ? nrm / len : glm::vec3(0.0f, 1.0f, 0.0f);
        faces.push_back(f);
    }

    const bool smooth = smoothAngleRad >= 0.0;
    std::vector<std::vector<int>> adj;
    float cosLimit = -1.0f;
    if (smooth) {
        adj.resize(np);
        for (size_t fi = 0; fi < faces.size(); ++fi)
            for (int k = 0; k < faces[fi].n; ++k) adj[(size_t)faces[fi].idx[k]].push_back((int)fi);
        cosLimit = (float)std::cos(std::min(smoothAngleRad, kPi)) - 1e-5f;
    }

    mesh.vertices.reserve(faces.size() * 4);
    mesh.indices.reserve(faces.size() * 6);
    const glm::vec3 color(0.8f);
    for (const Face& f : faces) {
        const uint32_t base = (uint32_t)mesh.vertices.size();
        for (int k = 0; k < f.n; ++k) {
            const size_t pi = (size_t)f.idx[k];
            glm::vec3 n = f.unit;
            if (smooth) {
                glm::vec3 sum(0.0f);
                for (int g : adj[pi])
                    if (glm::dot(f.unit, faces[(size_t)g].unit) >= cosLimit) sum += faces[(size_t)g].area;
                const float len = glm::length(sum);
                if (len > 1e-20f) n = sum / len;
            }
            mesh.vertices.push_back({pts[pi], color, n});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
        if (f.n == 4) mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 3});
    }
    updateBounds(mesh);
    return mesh;
}

TRS toIronTRS(const glm::dvec3& off, const glm::dvec3& v1, const glm::dvec3& v2, const glm::dvec3& v3) {
    // Espejo S = diag(1, 1, -1): A' = S A S, t' = S t.
    const glm::dvec3 s(1.0, 1.0, -1.0);
    glm::dvec3 col[3] = {v1 * s * s.x, v2 * s * s.y, v3 * s * s.z};
    TRS out;
    out.position = glm::vec3(glm::dvec3(off.x, off.y, -off.z) / kCmPerUnit);

    glm::dvec3 scl(glm::length(col[0]), glm::length(col[1]), glm::length(col[2]));
    const double det = glm::dot(col[0], glm::cross(col[1], col[2]));
    if (det < 0.0) scl.x = -scl.x; // escala negativa (espejo): se pone en X
    for (int i = 0; i < 3; ++i) col[i] = std::fabs(scl[i]) > 1e-12 ? col[i] / scl[i] : glm::dvec3(i == 0, i == 1, i == 2);

    // R = Rz(c) * Ry(b) * Rx(a); col[j][i] = fila i, columna j.
    const double r20 = col[0].z, r21 = col[1].z, r22 = col[2].z;
    const double r10 = col[0].y, r00 = col[0].x, r12 = col[2].y, r11 = col[1].y;
    const double b = std::asin(std::clamp(-r20, -1.0, 1.0));
    double a, c;
    if (std::cos(b) > 1e-6) {
        a = std::atan2(r21, r22);
        c = std::atan2(r10, r00);
    } else { // bloqueo de cardan: toda la rotacion restante va a X
        a = std::atan2(-r12, r11);
        c = 0.0;
    }
    const double toDeg = 180.0 / kPi;
    out.rotationDeg = glm::vec3((float)(a * toDeg), (float)(b * toDeg), (float)(c * toDeg));
    out.scale = glm::vec3(scl);
    return out;
}

// --- Exportar ---------------------------------------------------------------------

Matrix fromIronTRS(const glm::vec3& position, const glm::vec3& rotationDeg, const glm::vec3& scale) {
    // Misma matriz que SceneNode::localMatrix (T * Rz * Ry * Rx * S), en double.
    const double toRad = kPi / 180.0;
    const double a = rotationDeg.x * toRad, b = rotationDeg.y * toRad, c = rotationDeg.z * toRad;
    const double ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
    // Columnas de R = Rz(c) * Ry(b) * Rx(a).
    glm::dvec3 col[3] = {
        {cc * cb, sc * cb, -sb},
        {cc * sb * sa - sc * ca, sc * sb * sa + cc * ca, cb * sa},
        {cc * sb * ca + sc * sa, sc * sb * ca - cc * sa, cb * ca},
    };
    for (int i = 0; i < 3; ++i) col[i] *= (double)scale[i];
    // Espejo S = diag(1, 1, -1): A_c4d = S A S, off_c4d = S t (en cm).
    const glm::dvec3 s(1.0, 1.0, -1.0);
    Matrix m;
    m.v1 = col[0] * s * s.x;
    m.v2 = col[1] * s * s.y;
    m.v3 = col[2] * s * s.z;
    m.off = glm::dvec3(position.x, position.y, -position.z) * kCmPerUnit;
    return m;
}

glm::dvec3 fromIronPoint(const glm::vec3& p) {
    return glm::dvec3((double)p.x, (double)p.y, -(double)p.z) * kCmPerUnit;
}

PolySoup fromIronMesh(const MeshData& mesh, bool* smooth) {
    PolySoup out;
    if (smooth) *smooth = false;
    const size_t nv = mesh.vertices.size();

    // Unir vertices con la misma posicion exacta (la malla de render repite
    // cada punto en todas las caras que lo usan).
    std::vector<int> weld(nv, -1);
    std::map<std::array<float, 3>, int> seen;
    for (size_t i = 0; i < nv; ++i) {
        const glm::vec3& p = mesh.vertices[i].position;
        auto [it, inserted] = seen.emplace(std::array<float, 3>{p.x, p.y, p.z}, (int)seen.size());
        if (inserted) out.points.push_back(fromIronPoint(p));
        weld[i] = it->second;
    }

    struct Tri { int v[3]; glm::dvec3 n; };
    std::vector<Tri> tris;
    tris.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t r[3] = {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]};
        if (r[0] >= nv || r[1] >= nv || r[2] >= nv) continue;
        Tri t{{weld[r[0]], weld[r[1]], weld[r[2]]}, {}};
        if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]) continue; // degenerado
        const glm::vec3& p0 = mesh.vertices[r[0]].position;
        glm::vec3 fn = glm::cross(mesh.vertices[r[1]].position - p0, mesh.vertices[r[2]].position - p0);
        // Iron3D dibuja sin descartar caras traseras y algunas mallas propias
        // (el cubo) tienen triangulos con el giro al reves: manda la normal de
        // los vertices, que es la que se ve iluminada.
        const glm::vec3 vn = mesh.vertices[r[0]].normal + mesh.vertices[r[1]].normal + mesh.vertices[r[2]].normal;
        if (glm::dot(fn, vn) < 0.0f) {
            std::swap(t.v[1], t.v[2]);
            fn = -fn;
        }
        const float len = glm::length(fn);
        t.n = len > 1e-20f ? glm::dvec3(fn / len) : glm::dvec3(0.0);
        if (smooth && !*smooth && len > 1e-20f) {
            for (uint32_t k : r)
                if (glm::dot(mesh.vertices[k].normal, fn / len) < 0.9995f) *smooth = true;
        }
        tris.push_back(t);
    }

    // Iron3D (diestro) -> C4D (zurdo): el espejo invierte el giro, asi que se
    // invierte el orden de cada cara (como en toIronMesh, al reves).
    // Junta `a` y `b` en un cuadrilatero si comparten arista (x->y en `a`,
    // y->x en `b`), estan en el mismo plano y el resultado es convexo.
    auto tryQuad = [&](const Tri& a, const Tri& b) {
        if (glm::dot(a.n, b.n) < 0.99999) return false;
        for (int e = 0; e < 3; ++e) {
            const int x = a.v[e], y = a.v[(e + 1) % 3], p = a.v[(e + 2) % 3];
            int q = -1;
            for (int f = 0; f < 3; ++f)
                if (b.v[f] == y && b.v[(f + 1) % 3] == x) q = b.v[(f + 2) % 3];
            if (q < 0 || q == p) continue;
            const int quad[4] = {x, q, y, p}; // mismo giro que los dos triangulos
            // Normal en espacio de C4D: el espejo cambia el signo del producto
            // vectorial, por eso convexo es "< 0".
            const glm::dvec3 nC4d(a.n.x, a.n.y, -a.n.z);
            for (int k = 0; k < 4; ++k) {
                const glm::dvec3& p0 = out.points[(size_t)quad[k]];
                const glm::dvec3& p1 = out.points[(size_t)quad[(k + 1) % 4]];
                const glm::dvec3& p2 = out.points[(size_t)quad[(k + 2) % 4]];
                if (glm::dot(glm::cross(p1 - p0, p2 - p1), nC4d) >= 0.0) return false;
            }
            out.polys.push_back({quad[0], quad[3], quad[2], quad[1]});
            return true;
        }
        return false;
    };
    for (size_t i = 0; i < tris.size(); ++i) {
        if (i + 1 < tris.size() && tryQuad(tris[i], tris[i + 1])) {
            ++i;
            continue;
        }
        const Tri& t = tris[i];
        out.polys.push_back({t.v[0], t.v[2], t.v[1], t.v[1]});
    }
    return out;
}

} // namespace iron3d::c4d
