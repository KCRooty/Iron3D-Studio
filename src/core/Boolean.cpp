#include "Boolean.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace iron3d {

namespace {

constexpr float kEps = 1e-5f;

struct Plane {
    glm::vec3 n{0.0f, 1.0f, 0.0f};
    float w = 0.0f;
    void flip() { n = -n; w = -w; }
};

struct Poly {
    std::vector<Vertex> v;
    Plane plane;
    void flip() {
        std::reverse(v.begin(), v.end());
        for (Vertex& x : v) x.normal = -x.normal;
        plane.flip();
    }
};

Vertex lerpVertex(const Vertex& a, const Vertex& b, float t) {
    Vertex o;
    o.position = glm::mix(a.position, b.position, t);
    o.color = glm::mix(a.color, b.color, t);
    o.uv = glm::mix(a.uv, b.uv, t);
    const glm::vec3 n = glm::mix(a.normal, b.normal, t);
    o.normal = glm::length(n) > 1e-8f ? glm::normalize(n) : a.normal;
    return o;
}

bool planeOf(const std::vector<Vertex>& v, Plane& out) {
    // Newell: robusto con poligonos casi degenerados.
    glm::vec3 n(0.0f);
    for (size_t i = 0; i < v.size(); ++i) {
        const glm::vec3& a = v[i].position;
        const glm::vec3& b = v[(i + 1) % v.size()].position;
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const float l = glm::length(n);
    if (l < 1e-12f) return false;
    out.n = n / l;
    out.w = glm::dot(out.n, v[0].position);
    return true;
}

enum { kCoplanar = 0, kFront = 1, kBack = 2, kSpanning = 3 };

void splitPolygon(const Plane& pl, const Poly& poly, std::vector<Poly>& coFront, std::vector<Poly>& coBack,
                  std::vector<Poly>& front, std::vector<Poly>& back) {
    int polyType = 0;
    std::vector<int> types;
    types.reserve(poly.v.size());
    for (const Vertex& x : poly.v) {
        const float t = glm::dot(pl.n, x.position) - pl.w;
        const int ty = t < -kEps ? kBack : (t > kEps ? kFront : kCoplanar);
        polyType |= ty;
        types.push_back(ty);
    }
    switch (polyType) {
        case kCoplanar: (glm::dot(pl.n, poly.plane.n) > 0.0f ? coFront : coBack).push_back(poly); break;
        case kFront: front.push_back(poly); break;
        case kBack: back.push_back(poly); break;
        default: {
            std::vector<Vertex> f, b;
            for (size_t i = 0; i < poly.v.size(); ++i) {
                const size_t j = (i + 1) % poly.v.size();
                const int ti = types[i], tj = types[j];
                const Vertex& vi = poly.v[i];
                const Vertex& vj = poly.v[j];
                if (ti != kBack) f.push_back(vi);
                if (ti != kFront) b.push_back(vi);
                if ((ti | tj) == kSpanning) {
                    const float d = glm::dot(pl.n, vj.position - vi.position);
                    if (std::fabs(d) < 1e-20f) continue;
                    const float t = (pl.w - glm::dot(pl.n, vi.position)) / d;
                    const Vertex m = lerpVertex(vi, vj, std::clamp(t, 0.0f, 1.0f));
                    f.push_back(m);
                    b.push_back(m);
                }
            }
            if (f.size() >= 3) { Poly p; p.v = std::move(f); p.plane = poly.plane; front.push_back(std::move(p)); }
            if (b.size() >= 3) { Poly p; p.v = std::move(b); p.plane = poly.plane; back.push_back(std::move(p)); }
            break;
        }
    }
}

struct Node {
    bool hasPlane = false;
    Plane plane;
    std::unique_ptr<Node> front, back;
    std::vector<Poly> polys;

    void invert() {
        for (Poly& p : polys) p.flip();
        if (hasPlane) plane.flip();
        std::swap(front, back);
        if (front) front->invert();
        if (back) back->invert();
    }

    std::vector<Poly> clipPolygons(const std::vector<Poly>& list) const {
        if (!hasPlane) return list;
        std::vector<Poly> f, b;
        for (const Poly& p : list) splitPolygon(plane, p, f, b, f, b);
        if (front) f = front->clipPolygons(f);
        b = back ? back->clipPolygons(b) : std::vector<Poly>{};
        f.insert(f.end(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()));
        return f;
    }

    void clipTo(const Node& bsp) {
        polys = bsp.clipPolygons(polys);
        if (front) front->clipTo(bsp);
        if (back) back->clipTo(bsp);
    }

    void allPolygons(std::vector<Poly>& out) const {
        out.insert(out.end(), polys.begin(), polys.end());
        if (front) front->allPolygons(out);
        if (back) back->allPolygons(out);
    }

    void build(std::vector<Poly> list) {
        if (list.empty()) return;
        if (!hasPlane) {
            plane = list[0].plane;
            hasPlane = true;
        }
        std::vector<Poly> f, b;
        for (const Poly& p : list) splitPolygon(plane, p, polys, polys, f, b);
        if (!f.empty()) {
            if (!front) front = std::make_unique<Node>();
            front->build(std::move(f));
        }
        if (!b.empty()) {
            if (!back) back = std::make_unique<Node>();
            back->build(std::move(b));
        }
    }
};

std::vector<Poly> toPolys(const MeshData& m) {
    std::vector<Poly> out;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        Poly p;
        p.v = {m.vertices[m.indices[i]], m.vertices[m.indices[i + 1]], m.vertices[m.indices[i + 2]]};
        if (!planeOf(p.v, p.plane)) continue;  // se saltan los triangulos degenerados
        // El sentido de giro de las mallas de Iron3D no es fiable (el cubo base mezcla caras en ambos sentidos,
        // el visor no descarta caras traseras): manda la normal de los vertices, que si apunta hacia fuera.
        const glm::vec3 avg = p.v[0].normal + p.v[1].normal + p.v[2].normal;
        if (glm::length(avg) > 0.5f && glm::dot(avg, p.plane.n) < 0.0f) {
            std::reverse(p.v.begin(), p.v.end());
            p.plane.flip();
        }
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace

bool booleanMeshes(const MeshData& a, const MeshData& b, BoolOp op, MeshData& out, std::string* err) {
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    if (a.indices.empty() || b.indices.empty()) return fail("una de las mallas esta vacia");
    if (a.indices.size() / 3 + b.indices.size() / 3 > 6000) return fail("las mallas son demasiado grandes (mas de 6000 triangulos entre las dos)");
    Node na, nb;
    na.build(toPolys(a));
    nb.build(toPolys(b));
    switch (op) {
        case BoolOp::Union:
            na.clipTo(nb);
            nb.clipTo(na);
            nb.invert();
            nb.clipTo(na);
            nb.invert();
            break;
        case BoolOp::Subtract:
            na.invert();
            na.clipTo(nb);
            nb.clipTo(na);
            nb.invert();
            nb.clipTo(na);
            nb.invert();
            break;
        case BoolOp::Intersect:
            na.invert();
            nb.clipTo(na);
            nb.invert();
            na.clipTo(nb);
            nb.clipTo(na);
            break;
    }
    {
        std::vector<Poly> pb;
        nb.allPolygons(pb);
        na.build(std::move(pb));
    }
    if (op != BoolOp::Union) na.invert();
    std::vector<Poly> res;
    na.allPolygons(res);

    out = MeshData{};
    for (const Poly& p : res) {
        if (p.v.size() < 3) continue;
        const uint32_t base = (uint32_t)out.vertices.size();
        for (const Vertex& v : p.v) out.vertices.push_back(v);
        for (size_t i = 1; i + 1 < p.v.size(); ++i)
            out.indices.insert(out.indices.end(), {base, base + (uint32_t)i, base + (uint32_t)i + 1});
        out.faceTris.push_back((uint32_t)(p.v.size() - 2));
    }
    if (out.indices.empty()) return fail("el resultado esta vacio (los objetos no se tocan o uno contiene al otro en una sustraccion)");
    updateBounds(out);
    return true;
}

} // namespace iron3d
