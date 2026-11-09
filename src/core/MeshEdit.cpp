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
        Face f = flatFace(verts, pts, verts[colorVertex[start]].color, glm::vec3(0, 1, 0));
        f.sel = true;
        faces.push_back(f);
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

} // namespace iron3d
