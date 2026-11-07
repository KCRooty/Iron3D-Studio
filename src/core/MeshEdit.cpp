#include "MeshEdit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace iron3d {

namespace {

struct Face {
    std::vector<uint32_t> v;  // vertices de render, en el orden del contorno
    bool sel = false;
};

// Cada poligono de la topologia como cara de vertices de render: el
// vertice de render que usa cada punto del contorno dentro de ese poligono.
std::vector<Face> polyFaces(const MeshData& m, const MeshTopology& t) {
    std::vector<Face> faces(t.polys.size());
    for (size_t p = 0; p < t.polys.size(); ++p) {
        std::map<int, uint32_t> pointToVertex;
        for (int tri : t.polyTris[p])
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = m.indices[3 * (size_t)tri + (size_t)k];
                pointToVertex.emplace(t.vertexPoint[v], v);
            }
        for (int q : t.polys[p]) faces[p].v.push_back(pointToVertex[q]);
    }
    return faces;
}

// Normal de un contorno (Newell): vale para poligonos de cualquier tamano.
glm::vec3 newellNormal(const std::vector<glm::vec3>& pts) {
    glm::vec3 n(0.0f);
    for (size_t i = 0; i < pts.size(); ++i) {
        const glm::vec3& a = pts[i];
        const glm::vec3& b = pts[(i + 1) % pts.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return glm::length(n) > 1e-20f ? glm::normalize(n) : glm::vec3(0.0f);
}

// Cara plana nueva con vertices propios (normal de la cara, color de `colorFrom`).
Face flatFace(std::vector<Vertex>& verts, const std::vector<glm::vec3>& pts, const glm::vec3& colorFrom,
              const glm::vec3& fallbackNormal) {
    glm::vec3 n = newellNormal(pts);
    if (glm::length(n) < 0.5f) n = fallbackNormal;
    Face f;
    for (const glm::vec3& p : pts) {
        f.v.push_back((uint32_t)verts.size());
        verts.push_back({p, colorFrom, n});
    }
    return f;
}

MeshEditResult finish(std::vector<Vertex> verts, const std::vector<Face>& faces, int changed) {
    MeshEditResult r;
    r.changed = changed;
    r.mesh.vertices = std::move(verts);
    for (const Face& f : faces) {
        if (f.v.size() < 3) continue;
        for (size_t i = 1; i + 1 < f.v.size(); ++i) {
            if (f.sel) r.selectTris.push_back((int)(r.mesh.indices.size() / 3));
            r.mesh.indices.insert(r.mesh.indices.end(), {f.v[0], f.v[i], f.v[i + 1]});
        }
        r.mesh.faceTris.push_back((uint32_t)(f.v.size() - 2));
    }
    removeUnusedVertices(r.mesh);
    updateBounds(r.mesh);
    return r;
}

} // namespace

std::set<int> polysFromTris(const MeshTopology& topo, const std::vector<int>& tris) {
    std::set<int> out;
    for (int t : tris)
        if (t >= 0 && t < (int)topo.triPoly.size() && topo.triPoly[(size_t)t] >= 0) out.insert(topo.triPoly[(size_t)t]);
    return out;
}

MeshEditResult extrudePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys, float distance) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    std::set<int> sel;
    for (int p : polys)
        if (p >= 0 && p < (int)topo.polys.size()) sel.insert(p);
    if (sel.empty()) return finish(std::move(verts), faces, 0);

    // Direccion de cada punto: media de las normales de los poligonos
    // seleccionados que lo usan.
    std::map<int, glm::vec3> dir;
    for (int p : sel)
        for (int q : topo.polys[(size_t)p]) dir[q] += topo.polyNormals[(size_t)p];
    for (auto& [q, d] : dir) d = glm::length(d) > 1e-12f ? glm::normalize(d) : glm::vec3(0.0f);
    auto moved = [&](int q) { return topo.points[(size_t)q] + dir[q] * distance; };

    std::set<std::pair<int, int>> directed;
    for (int p : sel) {
        const auto& pl = topo.polys[(size_t)p];
        for (size_t k = 0; k < pl.size(); ++k) directed.insert({pl[k], pl[(k + 1) % pl.size()]});
    }
    std::map<uint32_t, uint32_t> dup;  // vertice de render -> copia desplazada (la tapa)
    auto dupOf = [&](uint32_t v) {
        auto it = dup.find(v);
        if (it != dup.end()) return it->second;
        Vertex nv = verts[v];
        nv.position = moved(topo.vertexPoint[v]);
        const uint32_t id = (uint32_t)verts.size();
        verts.push_back(nv);
        dup.emplace(v, id);
        return id;
    };
    std::vector<Face> walls;
    for (int p : sel) {
        const auto& pl = topo.polys[(size_t)p];
        Face& f = faces[(size_t)p];
        for (size_t k = 0; k < pl.size(); ++k) {
            const int a = pl[k], b = pl[(k + 1) % pl.size()];
            if (directed.count({b, a})) continue;  // arista interior de la region
            // Pared: comparte a'->b' con la tapa (que la recorre igual), asi
            // que va b' -> a' -> a -> b y el giro sigue siendo coherente.
            walls.push_back(flatFace(verts, {moved(b), moved(a), topo.points[(size_t)a], topo.points[(size_t)b]},
                                     verts[f.v[k]].color, topo.polyNormals[(size_t)p]));
        }
        for (uint32_t& v : f.v) v = dupOf(v);
        f.sel = true;
    }
    faces.insert(faces.end(), walls.begin(), walls.end());
    return finish(std::move(verts), faces, (int)sel.size());
}

MeshEditResult bevelPolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys, float inset,
                             float extrusion) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    inset = std::max(0.0f, inset);
    std::vector<Face> ring;
    int changed = 0;
    for (int p : polys) {
        if (p < 0 || p >= (int)topo.polys.size()) continue;
        const auto& pl = topo.polys[(size_t)p];
        const glm::vec3 n = topo.polyNormals[(size_t)p];
        const glm::vec3 c = polyCenter(topo, p);
        Face& f = faces[(size_t)p];
        std::vector<glm::vec3> outer, inner;
        for (int q : pl) {
            const glm::vec3 P = topo.points[(size_t)q];
            const glm::vec3 d = c - P;
            const float len = glm::length(d);
            const float s = len > 1e-9f ? std::min(inset, 0.95f * len) / len : 0.0f;
            outer.push_back(P);
            inner.push_back(P + d * s + n * extrusion);
        }
        for (size_t k = 0; k < pl.size(); ++k) {
            const size_t k1 = (k + 1) % pl.size();
            // Mismo sentido que el poligono: borde exterior a->b, luego hacia dentro.
            ring.push_back(flatFace(verts, {outer[k], outer[k1], inner[k1], inner[k]}, verts[f.v[k]].color, n));
        }
        for (size_t k = 0; k < pl.size(); ++k) {
            Vertex nv = verts[f.v[k]];
            nv.position = inner[k];
            nv.normal = n;
            f.v[k] = (uint32_t)verts.size();
            verts.push_back(nv);
        }
        f.sel = true;
        ++changed;
    }
    faces.insert(faces.end(), ring.begin(), ring.end());
    return finish(std::move(verts), faces, changed);
}

MeshEditResult knifeCut(const MeshData& mesh, const MeshTopology& topo, const glm::vec3& planePoint,
                        const glm::vec3& planeNormal, const std::vector<char>& allowed,
                        const std::function<bool(const glm::vec3&)>& inRange) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    const glm::vec3 n = glm::length(planeNormal) > 1e-20f ? glm::normalize(planeNormal) : glm::vec3(0, 1, 0);
    std::vector<float> dist(topo.points.size());
    float scale = 0.0f;
    for (size_t i = 0; i < topo.points.size(); ++i) {
        dist[i] = glm::dot(topo.points[i] - planePoint, n);
        scale = std::max(scale, std::fabs(dist[i]));
    }
    const float eps = std::max(1e-6f, scale * 1e-5f);
    std::vector<Face> out;
    int changed = 0;
    for (size_t p = 0; p < faces.size(); ++p) {
        const auto& pl = topo.polys[p];
        const Face& f = faces[p];
        if (!allowed.empty() && (p >= allowed.size() || !allowed[p])) {
            out.push_back(f);
            continue;
        }
        struct Entry { uint32_t v; bool cut; glm::vec3 pos; };
        std::vector<Entry> loop;
        for (size_t k = 0; k < pl.size(); ++k) {
            const size_t k1 = (k + 1) % pl.size();
            const int a = pl[k], b = pl[k1];
            const float da = dist[(size_t)a], db = dist[(size_t)b];
            loop.push_back({f.v[k], std::fabs(da) <= eps, topo.points[(size_t)a]});
            if ((da > eps && db < -eps) || (da < -eps && db > eps)) {
                // Punto de corte calculado siempre desde el extremo menor: las
                // dos caras que comparten la arista dan exactamente el mismo.
                const bool swap = a > b;
                const int lo = swap ? b : a, hi = swap ? a : b;
                const float t = dist[(size_t)lo] / (dist[(size_t)lo] - dist[(size_t)hi]);
                const glm::vec3 pos = topo.points[(size_t)lo] + (topo.points[(size_t)hi] - topo.points[(size_t)lo]) * t;
                const Vertex& va = verts[swap ? f.v[k1] : f.v[k]];
                const Vertex& vb = verts[swap ? f.v[k] : f.v[k1]];
                Vertex nv;
                nv.position = pos;
                nv.color = va.color + (vb.color - va.color) * t;
                const glm::vec3 nn = va.normal + (vb.normal - va.normal) * t;
                nv.normal = glm::length(nn) > 1e-12f ? glm::normalize(nn) : va.normal;
                loop.push_back({(uint32_t)verts.size(), true, pos});
                verts.push_back(nv);
            }
        }
        std::vector<size_t> cuts;
        for (size_t i = 0; i < loop.size(); ++i)
            if (loop[i].cut) cuts.push_back(i);
        bool split = cuts.size() == 2;
        if (split) {
            const size_t i = cuts[0], j = cuts[1];
            split = j - i >= 2 && loop.size() - (j - i) >= 2 && (!inRange || (inRange(loop[i].pos) && inRange(loop[j].pos)));
        }
        if (!split) {
            out.push_back(f);  // los vertices nuevos que no se usen se quitan al final
            continue;
        }
        const size_t i = cuts[0], j = cuts[1];
        Face A, B;
        for (size_t k = i; k <= j; ++k) A.v.push_back(loop[k].v);
        for (size_t k = j; k < loop.size(); ++k) B.v.push_back(loop[k].v);
        for (size_t k = 0; k <= i; ++k) B.v.push_back(loop[k].v);
        out.push_back(A);
        out.push_back(B);
        ++changed;
    }
    return finish(std::move(verts), out, changed);
}

} // namespace iron3d
