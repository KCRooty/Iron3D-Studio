#include "MeshEdit.h"

#include <algorithm>
#include <array>
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

MeshEditResult loopCut(const MeshData& mesh, const MeshTopology& topo, int edge, float t) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    if (edge < 0 || edge >= (int)topo.edges.size()) return finish(std::move(verts), faces, 0);
    t = std::clamp(t, 0.02f, 0.98f);
    const std::set<int> ring = edgeRing(topo, edge);
    std::map<std::pair<int, int>, int> edgeId;
    for (size_t e = 0; e < topo.edges.size(); ++e) edgeId[topo.edges[e]] = (int)e;
    auto eid = [&](int a, int b) {
        auto it = edgeId.find({std::min(a, b), std::max(a, b)});
        return it == edgeId.end() ? -1 : it->second;
    };
    std::vector<Face> out;
    int changed = 0;
    for (size_t p = 0; p < faces.size(); ++p) {
        const auto& pl = topo.polys[p];
        const Face& f = faces[p];
        if (pl.size() != 4) { out.push_back(f); continue; }
        int hit[4];
        int count = 0;
        for (int k = 0; k < 4; ++k) {
            hit[k] = ring.count(eid(pl[(size_t)k], pl[(size_t)(k + 1) % 4])) ? 1 : 0;
            count += hit[k];
        }
        // Solo cuadrilateros cortados por dos aristas opuestas (k y k+2).
        int k0 = -1;
        if (count == 2 && hit[0] && hit[2]) k0 = 0;
        else if (count == 2 && hit[1] && hit[3]) k0 = 1;
        if (k0 < 0) { out.push_back(f); continue; }
        // Nuevos vertices: el punto se calcula desde el extremo de menor indice
        // (las dos caras que comparten la arista dan exactamente el mismo).
        auto cutVertex = [&](size_t k) {
            const size_t k1 = (k + 1) % 4;
            const int a = pl[k], b = pl[k1];
            const bool swap = a > b;
            const int lo = swap ? b : a, hi = swap ? a : b;
            const glm::vec3 pos = topo.points[(size_t)lo] + (topo.points[(size_t)hi] - topo.points[(size_t)lo]) * t;
            const Vertex& va = verts[swap ? f.v[k1] : f.v[k]];
            const Vertex& vb = verts[swap ? f.v[k] : f.v[k1]];
            Vertex nv;
            nv.position = pos;
            nv.color = va.color + (vb.color - va.color) * t;
            nv.uv = va.uv + (vb.uv - va.uv) * t;
            const glm::vec3 nn = va.normal + (vb.normal - va.normal) * t;
            nv.normal = glm::length(nn) > 1e-12f ? glm::normalize(nn) : va.normal;
            verts.push_back(nv);
            return (uint32_t)(verts.size() - 1);
        };
        const size_t ka = (size_t)k0, kb = (size_t)k0 + 2;
        const uint32_t m1 = cutVertex(ka), m2 = cutVertex(kb);
        // v[ka] m1 v[ka+1] v[ka+2] m2 v[kb+1]: dos cuadrilateros.
        Face A, B;
        A.v = {m1, f.v[(ka + 1) % 4], f.v[(ka + 2) % 4], m2};
        B.v = {m2, f.v[(kb + 1) % 4], f.v[ka], m1};
        out.push_back(A);
        out.push_back(B);
        ++changed;
    }
    return finish(std::move(verts), out, changed);
}

// --- Seleccion por topologia ---------------------------------------------------

namespace {

// Aristas que salen de cada punto y poligonos de cada arista.
struct EdgeAdjacency {
    std::vector<std::vector<int>> pointEdges;
    std::vector<std::vector<int>> edgePolys;
    std::map<std::pair<int, int>, int> edgeIndex;
};

EdgeAdjacency edgeAdjacency(const MeshTopology& t) {
    EdgeAdjacency a;
    a.pointEdges.resize(t.points.size());
    a.edgePolys.resize(t.edges.size());
    for (size_t e = 0; e < t.edges.size(); ++e) {
        a.edgeIndex[t.edges[e]] = (int)e;
        a.pointEdges[(size_t)t.edges[e].first].push_back((int)e);
        a.pointEdges[(size_t)t.edges[e].second].push_back((int)e);
    }
    for (size_t p = 0; p < t.polys.size(); ++p) {
        const auto& pl = t.polys[p];
        for (size_t k = 0; k < pl.size(); ++k) {
            const int x = pl[k], y = pl[(k + 1) % pl.size()];
            auto it = a.edgeIndex.find({std::min(x, y), std::max(x, y)});
            if (it != a.edgeIndex.end()) a.edgePolys[(size_t)it->second].push_back((int)p);
        }
    }
    return a;
}

bool sharePoly(const EdgeAdjacency& a, int e0, int e1) {
    for (int p : a.edgePolys[(size_t)e0])
        for (int q : a.edgePolys[(size_t)e1])
            if (p == q) return true;
    return false;
}

// Vecinos de cada elemento: puntos por arista, aristas por punto comun y
// poligonos por punto comun.
std::vector<std::vector<int>> neighbours(const MeshTopology& t, int mode) {
    std::vector<std::vector<int>> out;
    if (mode == 1) {
        out.resize(t.points.size());
        for (const auto& [x, y] : t.edges) {
            out[(size_t)x].push_back(y);
            out[(size_t)y].push_back(x);
        }
        return out;
    }
    std::vector<std::vector<int>> byPoint(t.points.size());
    if (mode == 2) {
        out.resize(t.edges.size());
        for (size_t e = 0; e < t.edges.size(); ++e) {
            byPoint[(size_t)t.edges[e].first].push_back((int)e);
            byPoint[(size_t)t.edges[e].second].push_back((int)e);
        }
        for (size_t e = 0; e < t.edges.size(); ++e)
            for (int q : {t.edges[e].first, t.edges[e].second})
                for (int o : byPoint[(size_t)q])
                    if (o != (int)e) out[e].push_back(o);
    } else {
        out.resize(t.polys.size());
        for (size_t p = 0; p < t.polys.size(); ++p)
            for (int q : t.polys[p]) byPoint[(size_t)q].push_back((int)p);
        for (size_t p = 0; p < t.polys.size(); ++p) {
            std::set<int> s;
            for (int q : t.polys[p])
                for (int o : byPoint[(size_t)q])
                    if (o != (int)p) s.insert(o);
            out[p].assign(s.begin(), s.end());
        }
    }
    return out;
}

} // namespace

std::set<int> edgeLoop(const MeshTopology& topo, int edge) {
    std::set<int> out;
    if (edge < 0 || edge >= (int)topo.edges.size()) return out;
    const EdgeAdjacency a = edgeAdjacency(topo);
    out.insert(edge);
    for (int dirEnd = 0; dirEnd < 2; ++dirEnd) {
        int cur = edge;
        int p = dirEnd == 0 ? topo.edges[(size_t)edge].second : topo.edges[(size_t)edge].first;
        for (;;) {
            const auto& inc = a.pointEdges[(size_t)p];
            const bool border = a.edgePolys[(size_t)cur].size() == 1;
            int next = -1, found = 0;
            for (int o : inc) {
                if (o == cur) continue;
                const bool ok = border ? a.edgePolys[(size_t)o].size() == 1 : (inc.size() == 4 && !sharePoly(a, cur, o));
                if (ok) { next = o; ++found; }
            }
            if (found != 1 || out.count(next)) break;
            out.insert(next);
            const auto& ne = topo.edges[(size_t)next];
            p = ne.first == p ? ne.second : ne.first;
            cur = next;
        }
    }
    return out;
}

std::set<int> edgeRing(const MeshTopology& topo, int edge) {
    std::set<int> out;
    if (edge < 0 || edge >= (int)topo.edges.size()) return out;
    const EdgeAdjacency a = edgeAdjacency(topo);
    out.insert(edge);
    for (int startPoly : a.edgePolys[(size_t)edge]) {
        int cur = edge, poly = startPoly;
        for (;;) {
            const auto& pl = topo.polys[(size_t)poly];
            if (pl.size() != 4) break;
            int k = -1;
            for (int i = 0; i < 4; ++i) {
                const int x = pl[(size_t)i], y = pl[(size_t)((i + 1) % 4)];
                if (std::make_pair(std::min(x, y), std::max(x, y)) == topo.edges[(size_t)cur]) k = i;
            }
            if (k < 0) break;
            const int x = pl[(size_t)((k + 2) % 4)], y = pl[(size_t)((k + 3) % 4)];
            auto it = a.edgeIndex.find({std::min(x, y), std::max(x, y)});
            if (it == a.edgeIndex.end() || out.count(it->second)) break;
            cur = it->second;
            out.insert(cur);
            int nextPoly = -1;
            for (int q : a.edgePolys[(size_t)cur])
                if (q != poly) nextPoly = q;
            if (nextPoly < 0) break;
            poly = nextPoly;
        }
    }
    return out;
}

std::set<int> growSelection(const MeshTopology& topo, int mode, const std::set<int>& sel) {
    const auto nb = neighbours(topo, mode);
    std::set<int> out = sel;
    for (int i : sel)
        if (i >= 0 && i < (int)nb.size()) out.insert(nb[(size_t)i].begin(), nb[(size_t)i].end());
    return out;
}

std::set<int> patternSelection(const std::set<int>& sel, int step, int offset) {
    if (step < 2) return sel;
    std::set<int> out;
    int k = 0;
    for (int i : sel) {
        if (((k - offset) % step + step) % step == 0) out.insert(i);
        ++k;
    }
    return out;
}

std::set<int> shrinkSelection(const MeshTopology& topo, int mode, const std::set<int>& sel) {
    const auto nb = neighbours(topo, mode);
    std::set<int> out;
    for (int i : sel) {
        if (i < 0 || i >= (int)nb.size()) continue;
        bool inner = true;
        for (int o : nb[(size_t)i]) inner = inner && sel.count(o) > 0;
        if (inner) out.insert(i);
    }
    return out;
}

std::set<int> outlineSelection(const MeshTopology& topo, int mode, const std::set<int>& sel) {
    std::set<int> inner = shrinkSelection(topo, mode, sel);
    std::set<int> out;
    for (int i : sel)
        if (!inner.count(i)) out.insert(i);
    return out;
}

std::set<int> fillSelection(const MeshTopology& topo, const std::set<int>& sel) {
    const auto nb = neighbours(topo, 3);
    const int n = (int)nb.size();
    std::vector<int> comp((size_t)n, -1);
    std::vector<std::vector<int>> groups;
    for (int s = 0; s < n; ++s) {
        if (sel.count(s) || comp[(size_t)s] >= 0) continue;
        groups.emplace_back();
        std::vector<int> stack{s};
        comp[(size_t)s] = (int)groups.size() - 1;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            groups.back().push_back(i);
            for (int o : nb[(size_t)i])
                if (!sel.count(o) && comp[(size_t)o] < 0) { comp[(size_t)o] = comp[(size_t)i]; stack.push_back(o); }
        }
    }
    std::set<int> out = sel;
    if (groups.size() < 2) return out;
    size_t big = 0;
    for (size_t g = 1; g < groups.size(); ++g)
        if (groups[g].size() > groups[big].size()) big = g;
    for (size_t g = 0; g < groups.size(); ++g)
        if (g != big) out.insert(groups[g].begin(), groups[g].end());
    return out;
}

std::set<int> connectedSelection(const MeshTopology& topo, int mode, const std::set<int>& sel) {
    const auto nb = neighbours(topo, mode);
    std::set<int> out;
    std::vector<int> stack;
    for (int i : sel)
        if (i >= 0 && i < (int)nb.size() && out.insert(i).second) stack.push_back(i);
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        for (int o : nb[(size_t)i])
            if (out.insert(o).second) stack.push_back(o);
    }
    return out;
}

// --- Operaciones de malla --------------------------------------------------------

MeshEditResult weldPoints(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& points) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    std::set<int> sel;
    for (int q : points)
        if (q >= 0 && q < (int)topo.points.size()) sel.insert(q);
    if (sel.size() < 2) return finish(std::move(verts), faces, 0);
    glm::vec3 center(0.0f);
    for (int q : sel) center += topo.points[(size_t)q];
    center /= (float)sel.size();
    for (size_t v = 0; v < verts.size(); ++v)
        if (sel.count(topo.vertexPoint[v])) verts[v].position = center;
    // En cada cara, los puntos soldados seguidos se quedan en uno.
    auto rep = [&](uint32_t v) { const int q = topo.vertexPoint[v]; return sel.count(q) ? -1 : q; };
    std::vector<Face> out;
    for (const Face& f : faces) {
        Face g;
        for (size_t k = 0; k < f.v.size(); ++k) {
            const uint32_t v = f.v[k];
            if (!g.v.empty() && rep(g.v.back()) == rep(v)) continue;
            g.v.push_back(v);
        }
        while (g.v.size() > 1 && rep(g.v.front()) == rep(g.v.back())) g.v.pop_back();
        if (g.v.size() >= 3) out.push_back(g);
    }
    return finish(std::move(verts), out, (int)sel.size());
}

// Triangulacion por orejas de un contorno plano (en el orden dado). Devuelve
// ternas de indices del contorno; vacio si no se puede (degenerado).
std::vector<std::array<int, 3>> earClipContour(const std::vector<glm::vec3>& pts) {
    std::vector<std::array<int, 3>> out;
    const int n = (int)pts.size();
    if (n < 3) return out;
    const glm::vec3 nrm = newellNormal(pts);
    if (glm::length(nrm) < 0.5f) return out;
    std::vector<int> idx((size_t)n);
    for (int i = 0; i < n; ++i) idx[(size_t)i] = i;
    auto side = [&](int a, int b, int c) { return glm::dot(glm::cross(pts[(size_t)b] - pts[(size_t)a], pts[(size_t)c] - pts[(size_t)b]), nrm); };
    int guard = n * n + 8;
    while (idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size() && !clipped; ++i) {
            const int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (side(a, b, c) <= 1e-9f) continue;  // esquina concava o recta
            bool inside = false;
            for (int q : idx) {
                if (q == a || q == b || q == c) continue;
                if (side(a, b, q) >= -1e-9f && side(b, c, q) >= -1e-9f && side(c, a, q) >= -1e-9f) { inside = true; break; }
            }
            if (inside) continue;
            out.push_back({a, b, c});
            idx.erase(idx.begin() + (long)i);
            clipped = true;
        }
        if (!clipped) { out.clear(); return out; }
    }
    if (idx.size() == 3) out.push_back({idx[0], idx[1], idx[2]});
    return out;
}

MeshEditResult closeHoles(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& touchPoints) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    std::set<std::pair<int, int>> directed;
    std::map<std::pair<int, int>, uint32_t> edgeVertex;  // arista dirigida -> vertice de render de su origen
    for (size_t p = 0; p < topo.polys.size(); ++p) {
        const auto& pl = topo.polys[p];
        for (size_t k = 0; k < pl.size(); ++k) {
            directed.insert({pl[k], pl[(k + 1) % pl.size()]});
            edgeVertex[{pl[k], pl[(k + 1) % pl.size()]}] = faces[p].v[k];
        }
    }
    // Borde: a->b sin b->a. El agujero lo recorre al reves (b->a).
    std::map<int, int> holeNext;
    std::map<int, uint32_t> colorVertex;
    for (const auto& [x, y] : directed)
        if (!directed.count({y, x})) {
            holeNext[y] = x;
            colorVertex[y] = edgeVertex[{x, y}];
        }
    std::set<int> used;
    int changed = 0;
    for (const auto& [start, unused] : holeNext) {
        (void)unused;
        if (used.count(start)) continue;
        std::vector<int> loop;
        int q = start;
        bool closed = false;
        while (!used.count(q)) {
            used.insert(q);
            loop.push_back(q);
            auto it = holeNext.find(q);
            if (it == holeNext.end()) break;
            q = it->second;
            if (q == start) { closed = true; break; }
        }
        if (!closed || loop.size() < 3) continue;
        bool touched = touchPoints.empty();
        for (int lq : loop) touched = touched || touchPoints.count(lq) > 0;
        if (!touched) continue;
        std::vector<glm::vec3> pts;
        for (int lq : loop) pts.push_back(topo.points[(size_t)lq]);
        // Convexo: un solo poligono. Concavo: triangulos por orejas (los poligonos de Iron3D son convexos).
        const glm::vec3 nrm = newellNormal(pts);
        bool convex = true;
        for (size_t k = 0; k < pts.size() && convex; ++k) {
            const glm::vec3 a = pts[k], b = pts[(k + 1) % pts.size()], c = pts[(k + 2) % pts.size()];
            if (glm::dot(glm::cross(b - a, c - b), nrm) < -1e-9f) convex = false;
        }
        const std::vector<std::array<int, 3>> ears = convex ? std::vector<std::array<int, 3>>{} : earClipContour(pts);
        if (convex || ears.empty()) {
            Face f = flatFace(verts, pts, verts[colorVertex[start]].color, glm::vec3(0, 1, 0));
            f.sel = true;
            faces.push_back(f);
        } else {
            for (const auto& e : ears) {
                Face f = flatFace(verts, {pts[(size_t)e[0]], pts[(size_t)e[1]], pts[(size_t)e[2]]},
                                  verts[colorVertex[start]].color, glm::vec3(0, 1, 0));
                f.sel = true;
                faces.push_back(f);
            }
        }
        ++changed;
    }
    return finish(std::move(verts), faces, changed);
}

MeshEditResult subdividePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    std::set<int> sel;
    for (int p : polys)
        if (p >= 0 && p < (int)topo.polys.size()) sel.insert(p);
    if (polys.empty())
        for (int p = 0; p < (int)topo.polys.size(); ++p) sel.insert(p);
    if (sel.empty()) return finish(std::move(verts), faces, 0);

    auto blend = [](const std::vector<Vertex>& vs) {
        Vertex r{};
        glm::vec3 n(0.0f);
        for (const Vertex& v : vs) { r.position += v.position; r.color += v.color; n += v.normal; }
        r.position /= (float)vs.size();
        r.color /= (float)vs.size();
        r.normal = glm::length(n) > 1e-12f ? glm::normalize(n) : vs.front().normal;
        return r;
    };
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> mids;
    auto mid = [&](uint32_t a, uint32_t b) {
        const auto key = std::make_pair(std::min(a, b), std::max(a, b));
        auto it = mids.find(key);
        if (it != mids.end()) return it->second;
        const uint32_t id = (uint32_t)verts.size();
        verts.push_back(blend({verts[a], verts[b]}));
        mids.emplace(key, id);
        return id;
    };
    std::set<std::pair<int, int>> cutEdges;  // aristas (en puntos) que se parten
    for (int p : sel) {
        const auto& pl = topo.polys[(size_t)p];
        for (size_t k = 0; k < pl.size(); ++k) {
            const int x = pl[k], y = pl[(k + 1) % pl.size()];
            cutEdges.insert({std::min(x, y), std::max(x, y)});
        }
    }
    std::vector<Face> out;
    for (size_t p = 0; p < faces.size(); ++p) {
        const auto& pl = topo.polys[p];
        const Face& f = faces[p];
        const size_t n = f.v.size();
        if (!sel.count((int)p)) {
            // Vecino sin seleccionar: recibe el punto medio en las aristas
            // partidas, asi no quedan puntos en T.
            Face g;
            for (size_t k = 0; k < n; ++k) {
                g.v.push_back(f.v[k]);
                const int x = pl[k], y = pl[(k + 1) % n];
                if (cutEdges.count({std::min(x, y), std::max(x, y)})) g.v.push_back(mid(f.v[k], f.v[(k + 1) % n]));
            }
            out.push_back(g);
            continue;
        }
        std::vector<Vertex> all;
        for (uint32_t v : f.v) all.push_back(verts[v]);
        const uint32_t c = (uint32_t)verts.size();
        verts.push_back(blend(all));
        for (size_t k = 0; k < n; ++k) {
            Face q;
            q.v = {f.v[k], mid(f.v[k], f.v[(k + 1) % n]), c, mid(f.v[(k + n - 1) % n], f.v[k])};
            q.sel = true;
            out.push_back(q);
        }
    }
    return finish(std::move(verts), out, (int)sel.size());
}

MeshEditResult flipPolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys) {
    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    int changed = 0;
    std::map<uint32_t, uint32_t> flipped;  // vertice -> copia con la normal al reves
    for (int p : polys) {
        if (p < 0 || p >= (int)faces.size()) continue;
        Face& f = faces[(size_t)p];
        std::reverse(f.v.begin(), f.v.end());
        for (uint32_t& v : f.v) {
            auto it = flipped.find(v);
            if (it == flipped.end()) {
                Vertex nv = verts[v];
                nv.normal = -nv.normal;
                it = flipped.emplace(v, (uint32_t)verts.size()).first;
                verts.push_back(nv);
            }
            v = it->second;
        }
        f.sel = true;
        ++changed;
    }
    return finish(std::move(verts), faces, changed);
}


MeshEditResult triangulatePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys) {
    std::vector<Vertex> verts = mesh.vertices;
    const std::vector<Face> faces = polyFaces(mesh, topo);
    std::vector<Face> out;
    int changed = 0;
    for (size_t p = 0; p < faces.size(); ++p) {
        const Face& f = faces[p];
        if (f.v.size() <= 3 || (!polys.empty() && !polys.count((int)p))) {
            out.push_back(f);
            continue;
        }
        // Abanico desde el primer punto: n-2 triangulos (bien para poligonos convexos, que son los de Iron3D).
        for (size_t i = 1; i + 1 < f.v.size(); ++i) {
            Face t;
            t.v = {f.v[0], f.v[i], f.v[i + 1]};
            t.sel = true;
            out.push_back(std::move(t));
        }
        ++changed;
    }
    return finish(std::move(verts), out, changed);
}

MeshEditResult untriangulatePolygons(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys,
                                     float maxAngleDeg) {
    std::vector<Vertex> verts = mesh.vertices;
    const std::vector<Face> faces = polyFaces(mesh, topo);
    const float cosMax = std::cos(glm::radians(maxAngleDeg));
    const int np = (int)topo.polys.size();
    auto eligible = [&](int p) { return topo.polys[(size_t)p].size() == 3 && (polys.empty() || polys.count(p)); };
    // Arista dirigida (a,b) -> triangulo que la recorre.
    std::map<std::pair<int, int>, int> dirEdge;
    for (int p = 0; p < np; ++p) {
        if (!eligible(p)) continue;
        const auto& pl = topo.polys[(size_t)p];
        for (size_t k = 0; k < 3; ++k) dirEdge[{pl[k], pl[(k + 1) % 3]}] = p;
    }
    std::vector<char> used((size_t)np, 0);
    std::vector<Face> out;
    int changed = 0;
    for (int p = 0; p < np; ++p) {
        if (used[(size_t)p]) continue;
        bool merged = false;
        if (eligible(p)) {
            const auto& pl = topo.polys[(size_t)p];
            for (size_t k = 0; k < 3 && !merged; ++k) {
                const int u = pl[k], v = pl[(k + 1) % 3], x = pl[(k + 2) % 3];
                const auto it = dirEdge.find({v, u});
                if (it == dirEdge.end()) continue;
                const int q = it->second;
                if (q == p || used[(size_t)q] || q < p) continue;
                if (glm::dot(topo.polyNormals[(size_t)p], topo.polyNormals[(size_t)q]) < cosMax) continue;
                const auto& ql = topo.polys[(size_t)q];
                int y = -1, yk = -1;
                for (size_t j = 0; j < 3; ++j)
                    if (ql[j] != u && ql[j] != v) { y = ql[j]; yk = (int)j; }
                if (y < 0) continue;
                // Cuadrilatero u, y, v, x: convexo si todos los giros miran como la normal.
                const glm::vec3 quad[4] = {topo.points[(size_t)u], topo.points[(size_t)y], topo.points[(size_t)v],
                                           topo.points[(size_t)x]};
                const glm::vec3 n = glm::normalize(topo.polyNormals[(size_t)p] + topo.polyNormals[(size_t)q]);
                bool convex = true;
                for (int i = 0; i < 4 && convex; ++i) {
                    const glm::vec3 e1 = quad[(i + 1) % 4] - quad[i], e2 = quad[(i + 2) % 4] - quad[(i + 1) % 4];
                    if (glm::dot(glm::cross(e1, e2), n) <= 1e-9f) convex = false;
                }
                if (!convex) continue;
                Face f;
                f.v = {faces[(size_t)p].v[k], faces[(size_t)q].v[(size_t)yk], faces[(size_t)p].v[(k + 1) % 3],
                       faces[(size_t)p].v[(k + 2) % 3]};
                f.sel = true;
                out.push_back(std::move(f));
                used[(size_t)p] = used[(size_t)q] = 1;
                ++changed;
                merged = true;
            }
        }
        if (!merged && !used[(size_t)p]) out.push_back(faces[(size_t)p]);
    }
    return finish(std::move(verts), out, changed);
}

MeshEditResult alignNormals(const MeshData& mesh, const MeshTopology& topo, const std::set<int>& polys) {
    // Arista dirigida (a,b) -> poligonos de `polys` que la recorren en ese sentido.
    std::map<std::pair<int, int>, std::vector<int>> dirEdge;
    for (int p : polys) {
        if (p < 0 || p >= (int)topo.polys.size()) continue;
        const auto& pl = topo.polys[(size_t)p];
        for (size_t k = 0; k < pl.size(); ++k) dirEdge[{pl[k], pl[(k + 1) % pl.size()]}].push_back(p);
    }
    std::map<int, bool> flipped;  // poligono visitado -> hay que invertirlo
    for (int start : polys) {
        if (start < 0 || start >= (int)topo.polys.size() || flipped.count(start)) continue;
        flipped[start] = false;
        std::vector<int> stack{start};
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            const bool pFlip = flipped[p];
            const auto& pl = topo.polys[(size_t)p];
            for (size_t k = 0; k < pl.size(); ++k) {
                const int a = pl[k], b = pl[(k + 1) % pl.size()];
                // Vecino coherente recorre (b,a); si recorre (a,b) como p, esta al reves.
                for (int same : dirEdge[{a, b}])
                    if (same != p && !flipped.count(same)) { flipped[same] = !pFlip; stack.push_back(same); }
                for (int opp : dirEdge[{b, a}])
                    if (opp != p && !flipped.count(opp)) { flipped[opp] = pFlip; stack.push_back(opp); }
            }
        }
    }
    std::set<int> toFlip;
    for (const auto& [p, f] : flipped)
        if (f) toFlip.insert(p);
    if (toFlip.empty()) return {};
    return flipPolygons(mesh, topo, toFlip);
}

MeshEditResult bridgePolygons(const MeshData& mesh, const MeshTopology& topo, int polyA, int polyB) {
    const int np = (int)topo.polys.size();
    if (polyA == polyB || polyA < 0 || polyB < 0 || polyA >= np || polyB >= np) return {};
    const auto& A = topo.polys[(size_t)polyA];
    const auto& B = topo.polys[(size_t)polyB];
    const int n = (int)A.size();
    if (n < 3 || (int)B.size() != n) return {};
    // Mejor emparejamiento: direccion (dir) y giro (shift) con la menor suma de distancias^2.
    float best = 1e30f;
    int bestDir = 1, bestShift = 0;
    for (int dir = -1; dir <= 1; dir += 2)
        for (int shift = 0; shift < n; ++shift) {
            float cost = 0.0f;
            for (int i = 0; i < n; ++i) {
                const glm::vec3 d = topo.points[(size_t)A[(size_t)i]] - topo.points[(size_t)B[(size_t)(((dir * i + shift) % n + n) % n)]];
                cost += glm::dot(d, d);
            }
            if (cost < best) {
                best = cost;
                bestDir = dir;
                bestShift = shift;
            }
        }
    auto bAt = [&](int i) { return B[(size_t)(((bestDir * i + bestShift) % n + n) % n)]; };
    glm::vec3 cA(0.0f), cB(0.0f);
    for (int i = 0; i < n; ++i) {
        cA += topo.points[(size_t)A[(size_t)i]];
        cB += topo.points[(size_t)B[(size_t)i]];
    }
    cA /= (float)n;
    cB /= (float)n;
    glm::vec3 axis = cB - cA;
    axis = glm::length(axis) > 1e-12f ? glm::normalize(axis) : glm::vec3(0, 1, 0);

    std::vector<Vertex> verts = mesh.vertices;
    std::vector<Face> faces = polyFaces(mesh, topo);
    const glm::vec3 color = verts.empty() ? glm::vec3(1.0f) : verts[0].color;
    int changed = 0;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        std::vector<glm::vec3> quad = {topo.points[(size_t)A[(size_t)i]], topo.points[(size_t)A[(size_t)j]],
                                       topo.points[(size_t)bAt(j)], topo.points[(size_t)bAt(i)]};
        glm::vec3 qc = (quad[0] + quad[1] + quad[2] + quad[3]) * 0.25f;
        glm::vec3 radial = (qc - cA) - axis * glm::dot(qc - cA, axis);
        glm::vec3 nrm = newellNormal(quad);
        if (glm::dot(nrm, radial) < 0.0f) std::reverse(quad.begin(), quad.end());
        Face f = flatFace(verts, quad, color, radial);
        f.sel = true;
        faces.push_back(f);
        ++changed;
    }
    // Fuera los dos poligonos originales.
    faces[(size_t)polyA].v.clear();
    faces[(size_t)polyB].v.clear();
    return finish(std::move(verts), faces, changed);
}

MeshCheck checkMesh(const MeshTopology& topo) {
    MeshCheck r;
    std::map<std::pair<int, int>, int> uses;
    std::vector<char> pointUsed(topo.points.size(), 0);
    for (size_t p = 0; p < topo.polys.size(); ++p) {
        const auto& pl = topo.polys[p];
        std::vector<glm::vec3> pts;
        for (size_t k = 0; k < pl.size(); ++k) {
            const int a = pl[k], b = pl[(k + 1) % pl.size()];
            ++uses[{std::min(a, b), std::max(a, b)}];
            pointUsed[(size_t)a] = 1;
            pts.push_back(topo.points[(size_t)a]);
        }
        // Area ~ 0 (longitud de la normal de Newell = 2 * area).
        glm::vec3 n(0.0f);
        for (size_t i = 0; i < pts.size(); ++i) {
            const glm::vec3& a = pts[i];
            const glm::vec3& b = pts[(i + 1) % pts.size()];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        if (pl.size() < 3 || glm::length(n) < 1e-10f) {
            ++r.degeneratePolys;
            r.degeneratePolySet.insert((int)p);
        }
    }
    for (size_t e = 0; e < topo.edges.size(); ++e) {
        auto it = uses.find(topo.edges[e]);
        const int u = it == uses.end() ? 0 : it->second;
        if (u == 1) {
            ++r.openEdges;
            r.openEdgeSet.insert((int)e);
        } else if (u > 2) {
            ++r.nonManifoldEdges;
            r.nonManifoldEdgeSet.insert((int)e);
        }
    }
    for (size_t q = 0; q < pointUsed.size(); ++q)
        if (!pointUsed[q]) {
            ++r.loosePoints;
            r.loosePointSet.insert((int)q);
        }
    return r;
}

} // namespace iron3d
