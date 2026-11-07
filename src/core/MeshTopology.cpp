#include "MeshTopology.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace iron3d {

namespace {

struct OrientedTri {
    int p[3];          // puntos, girando segun la normal de los vertices
    glm::vec3 n;       // normal geometrica (sin normalizar: lleva el area)
    bool degenerate;
};

// Triangulo t en puntos, con el giro corregido: Iron3D dibuja sin descartar
// caras traseras y algunas mallas propias (el cubo) traen triangulos con el
// giro al reves; manda la normal de los vertices, que es la que se ve.
OrientedTri orientTri(const MeshData& m, const std::vector<int>& vpt, size_t t) {
    OrientedTri o{};
    const uint32_t r[3] = {m.indices[3 * t], m.indices[3 * t + 1], m.indices[3 * t + 2]};
    const size_t nv = m.vertices.size();
    if (r[0] >= nv || r[1] >= nv || r[2] >= nv) {
        o.degenerate = true;
        return o;
    }
    for (int k = 0; k < 3; ++k) o.p[k] = vpt[r[k]];
    const glm::vec3& p0 = m.vertices[r[0]].position;
    o.n = glm::cross(m.vertices[r[1]].position - p0, m.vertices[r[2]].position - p0);
    o.degenerate = o.p[0] == o.p[1] || o.p[1] == o.p[2] || o.p[0] == o.p[2] || glm::length(o.n) < 1e-20f;
    const glm::vec3 vn = m.vertices[r[0]].normal + m.vertices[r[1]].normal + m.vertices[r[2]].normal;
    if (!o.degenerate && glm::dot(o.n, vn) < 0.0f) {
        std::swap(o.p[1], o.p[2]);
        o.n = -o.n;
    }
    return o;
}

std::vector<int> weldPoints(const MeshData& m, std::vector<glm::vec3>* pts) {
    std::vector<int> vpt(m.vertices.size(), 0);
    std::map<std::tuple<long long, long long, long long>, int> byPos;
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const glm::vec3 p = m.vertices[i].position;
        const auto k = std::make_tuple((long long)std::llround(p.x * 1e5), (long long)std::llround(p.y * 1e5),
                                       (long long)std::llround(p.z * 1e5));
        auto it = byPos.find(k);
        if (it == byPos.end()) {
            it = byPos.emplace(k, (int)byPos.size()).first;
            if (pts) pts->push_back(p);
        }
        vpt[i] = it->second;
    }
    return vpt;
}

// Dos triangulos (ya orientados) forman un cuadrilatero convexo y plano.
bool formsQuad(const OrientedTri& a, const OrientedTri& b, const std::vector<glm::vec3>& pts) {
    if (a.degenerate || b.degenerate) return false;
    const glm::vec3 na = glm::normalize(a.n), nb = glm::normalize(b.n);
    if (glm::dot(na, nb) < 0.9999f) return false;
    for (int e = 0; e < 3; ++e) {
        const int x = a.p[e], y = a.p[(e + 1) % 3], p = a.p[(e + 2) % 3];
        int q = -1;
        for (int f = 0; f < 3; ++f)
            if (b.p[f] == y && b.p[(f + 1) % 3] == x) q = b.p[(f + 2) % 3];
        if (q < 0 || q == p) continue;
        const int quad[4] = {x, q, y, p};
        for (int k = 0; k < 4; ++k) {
            const glm::vec3& p0 = pts[(size_t)quad[k]];
            const glm::vec3& p1 = pts[(size_t)quad[(k + 1) % 4]];
            const glm::vec3& p2 = pts[(size_t)quad[(k + 2) % 4]];
            if (glm::dot(glm::cross(p1 - p0, p2 - p1), na) <= 0.0f) return false;
        }
        return true;
    }
    return false;
}

// Contorno de una cara a partir de sus triangulos orientados: las aristas
// dirigidas sin pareja inversa, encadenadas. Vacio si no sale un solo lazo.
std::vector<int> faceBoundary(const std::vector<OrientedTri>& tris) {
    std::set<std::pair<int, int>> directed;
    for (const auto& t : tris)
        for (int k = 0; k < 3; ++k) directed.insert({t.p[k], t.p[(k + 1) % 3]});
    std::map<int, int> next;
    size_t count = 0;
    for (const auto& [a, b] : directed) {
        if (directed.count({b, a})) continue;
        if (next.count(a)) return {};
        next[a] = b;
        ++count;
    }
    if (count < 3) return {};
    std::vector<int> loop;
    int cur = next.begin()->first;
    for (size_t i = 0; i < count; ++i) {
        loop.push_back(cur);
        auto it = next.find(cur);
        if (it == next.end()) return {};
        cur = it->second;
    }
    if (cur != loop.front()) return {};
    return loop;
}

} // namespace

std::vector<uint32_t> faceTriCounts(const MeshData& mesh) {
    const size_t nt = mesh.indices.size() / 3;
    if (!mesh.faceTris.empty()) {
        size_t sum = 0;
        for (uint32_t c : mesh.faceTris) sum += c;
        if (sum == nt) return mesh.faceTris;
    }
    std::vector<glm::vec3> pts;
    const std::vector<int> vpt = weldPoints(mesh, &pts);
    std::vector<uint32_t> out;
    out.reserve(nt);
    for (size_t t = 0; t < nt; ++t) {
        if (t + 1 < nt && formsQuad(orientTri(mesh, vpt, t), orientTri(mesh, vpt, t + 1), pts)) {
            out.push_back(2);
            ++t;
        } else {
            out.push_back(1);
        }
    }
    return out;
}

MeshTopology buildMeshTopology(const MeshData& mesh) {
    MeshTopology topo;
    topo.vertexPoint = weldPoints(mesh, &topo.points);
    const size_t nt = mesh.indices.size() / 3;
    topo.triPoly.assign(nt, -1);
    const std::vector<uint32_t> faces = faceTriCounts(mesh);
    std::set<std::pair<int, int>> edges;
    size_t t0 = 0;
    for (uint32_t count : faces) {
        std::vector<OrientedTri> tris;
        std::vector<int> triIds;
        for (size_t t = t0; t < t0 + count && t < nt; ++t) {
            const OrientedTri o = orientTri(mesh, topo.vertexPoint, t);
            if (o.degenerate) continue;
            tris.push_back(o);
            triIds.push_back((int)t);
        }
        t0 += count;
        if (tris.empty()) continue;
        std::vector<std::vector<int>> loops;
        std::vector<std::vector<int>> loopTris;
        std::vector<int> loop = tris.size() == 1 ? std::vector<int>{tris[0].p[0], tris[0].p[1], tris[0].p[2]}
                                                 : faceBoundary(tris);
        if (!loop.empty()) {
            loops.push_back(loop);
            loopTris.push_back(triIds);
        } else {
            // Cara rara (no sale un lazo): cada triangulo es su poligono.
            for (size_t i = 0; i < tris.size(); ++i) {
                loops.push_back({tris[i].p[0], tris[i].p[1], tris[i].p[2]});
                loopTris.push_back({triIds[i]});
            }
        }
        for (size_t l = 0; l < loops.size(); ++l) {
            const int id = (int)topo.polys.size();
            glm::vec3 pn(0.0f);
            for (int t : loopTris[l]) {
                topo.triPoly[(size_t)t] = id;
                pn += orientTri(mesh, topo.vertexPoint, (size_t)t).n;
            }
            topo.polyNormals.push_back(glm::length(pn) > 1e-20f ? glm::normalize(pn) : glm::vec3(0, 1, 0));
            const auto& lp = loops[l];
            for (size_t k = 0; k < lp.size(); ++k) {
                const int a = lp[k], b = lp[(k + 1) % lp.size()];
                edges.insert({std::min(a, b), std::max(a, b)});
            }
            topo.polys.push_back(lp);
            topo.polyTris.push_back(loopTris[l]);
        }
    }
    topo.edges.assign(edges.begin(), edges.end());
    return topo;
}

glm::vec3 polyCenter(const MeshTopology& topo, int poly) {
    glm::vec3 c(0.0f);
    const auto& p = topo.polys[(size_t)poly];
    for (int i : p) c += topo.points[(size_t)i];
    return p.empty() ? c : c / (float)p.size();
}

void refreshTopologyPositions(MeshTopology& topo, const MeshData& mesh) {
    for (size_t v = 0; v < mesh.vertices.size() && v < topo.vertexPoint.size(); ++v)
        topo.points[(size_t)topo.vertexPoint[v]] = mesh.vertices[v].position;
    for (size_t p = 0; p < topo.polys.size(); ++p) {
        glm::vec3 n(0.0f);
        for (int t : topo.polyTris[p]) n += orientTri(mesh, topo.vertexPoint, (size_t)t).n;
        if (glm::length(n) > 1e-20f) topo.polyNormals[p] = glm::normalize(n);
    }
}

void removeUnusedVertices(MeshData& mesh) {
    std::vector<uint32_t> remap(mesh.vertices.size(), UINT32_MAX);
    std::vector<Vertex> verts;
    verts.reserve(mesh.vertices.size());
    for (uint32_t& i : mesh.indices) {
        if (i >= mesh.vertices.size()) continue;
        if (remap[i] == UINT32_MAX) {
            remap[i] = (uint32_t)verts.size();
            verts.push_back(mesh.vertices[i]);
        }
        i = remap[i];
    }
    mesh.vertices = std::move(verts);
}

ComponentVisibility computeComponentVisibility(const MeshData& mesh, const MeshTopology& topo, const glm::mat4& mvp,
                                               int width, int height) {
    ComponentVisibility vis;
    const int W = std::max(1, width / 2), H = std::max(1, height / 2);
    // Se guarda 1/w (mas grande = mas cerca); 0 = fondo.
    std::vector<float> zinv((size_t)W * (size_t)H, 0.0f);
    struct SV { float x, y, iw; bool ok; };
    auto project = [&](const glm::vec3& p) {
        const glm::vec4 c = mvp * glm::vec4(p, 1.0f);
        if (c.w <= 1e-4f) return SV{0, 0, 0, false};
        return SV{(c.x / c.w * 0.5f + 0.5f) * (float)W, (0.5f - c.y / c.w * 0.5f) * (float)H, 1.0f / c.w, true};
    };
    std::vector<SV> sv(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) sv[i] = project(mesh.vertices[i].position);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        if (ia >= sv.size() || ib >= sv.size() || ic >= sv.size()) continue;
        const SV a = sv[ia], b = sv[ib], c = sv[ic];
        if (!a.ok || !b.ok || !c.ok) continue;
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-12f) continue;
        const int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})));
        const int x1 = std::min(W - 1, (int)std::ceil(std::max({a.x, b.x, c.x})));
        const int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
        const int y1 = std::min(H - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = (float)x + 0.5f, py = (float)y + 0.5f;
                const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                const float iw = w0 * a.iw + w1 * b.iw + w2 * c.iw;
                float& z = zinv[(size_t)y * (size_t)W + (size_t)x];
                if (iw > z) z = iw;
            }
    }
    auto visible = [&](const glm::vec3& p) -> char {
        const SV s = project(p);
        if (!s.ok) return 0;
        const int cx = (int)std::floor(s.x), cy = (int)std::floor(s.y);
        if (cx < -1 || cy < -1 || cx > W || cy > H) return 1;  // fuera del visor: no lo tapa nada de lo dibujado
        // Lo mas lejano del entorno de 3x3 pixeles: un punto en una silueta no
        // queda tapado por la propia cara a la que pertenece.
        float farthest = 1e30f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx, y = cy + dy;
                const float z = (x < 0 || y < 0 || x >= W || y >= H) ? 0.0f : zinv[(size_t)y * (size_t)W + (size_t)x];
                farthest = std::min(farthest, z);
            }
        return s.iw >= farthest * 0.99f ? 1 : 0;
    };
    vis.points.resize(topo.points.size());
    for (size_t i = 0; i < topo.points.size(); ++i) vis.points[i] = visible(topo.points[i]);
    vis.edges.resize(topo.edges.size());
    for (size_t i = 0; i < topo.edges.size(); ++i)
        vis.edges[i] = visible((topo.points[(size_t)topo.edges[i].first] + topo.points[(size_t)topo.edges[i].second]) * 0.5f);
    vis.polys.resize(topo.polys.size());
    for (size_t i = 0; i < topo.polys.size(); ++i) vis.polys[i] = visible(polyCenter(topo, (int)i));
    return vis;
}

MeshData keepPolygons(const MeshData& mesh, const MeshTopology& topo, const std::vector<char>& keep) {
    MeshData out;
    out.vertices = mesh.vertices;
    for (size_t p = 0; p < topo.polys.size(); ++p) {
        if (p < keep.size() && !keep[p]) continue;
        for (int t : topo.polyTris[p])
            for (int k = 0; k < 3; ++k) out.indices.push_back(mesh.indices[3 * (size_t)t + (size_t)k]);
        out.faceTris.push_back((uint32_t)topo.polyTris[p].size());
    }
    removeUnusedVertices(out);
    updateBounds(out);
    return out;
}

} // namespace iron3d
