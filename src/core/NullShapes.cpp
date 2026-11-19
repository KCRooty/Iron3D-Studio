#include "NullShapes.h"

#include <cmath>

namespace iron3d {
namespace {

constexpr float kPi = 3.14159265358979f;

// Plano de la forma: ejes u, v segun la orientacion (Pantalla se dibuja como XY).
void planeAxes(int orient, glm::vec3& u, glm::vec3& v) {
    switch (orient) {
        case 2: u = {0, 0, 1}; v = {0, 1, 0}; break; // ZY
        case 3: u = {1, 0, 0}; v = {0, 0, 1}; break; // XZ
        default: u = {1, 0, 0}; v = {0, 1, 0}; break; // XY / Pantalla
    }
}

void loop(std::vector<glm::vec3>& out, const std::vector<glm::vec3>& pts) {
    for (size_t i = 0; i < pts.size(); ++i) {
        out.push_back(pts[i]);
        out.push_back(pts[(i + 1) % pts.size()]);
    }
}

std::vector<glm::vec3> ngon(int n, float r, const glm::vec3& u, const glm::vec3& v, float phase) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < n; ++i) {
        const float a = phase + 2.0f * kPi * (float)i / (float)n;
        p.push_back(u * (std::cos(a) * r) + v * (std::sin(a) * r));
    }
    return p;
}

} // namespace

void nullShapeLocalLines(const NullDisplay& d, std::vector<glm::vec3>& out) {
    if (d.shape < 0 || d.shape == NullNone) return;
    const float r = std::max(d.radius, 1e-5f);
    const float a = std::max(d.aspect, 1e-3f);
    glm::vec3 u, v;
    planeAxes(d.orientation, u, v);
    switch (d.shape) {
        case NullDot:
        case NullPoint: {
            const float s = std::min(r, 0.01f);
            for (int i = 0; i < 3; ++i) {
                glm::vec3 e(0.0f);
                e[i] = s;
                out.push_back(-e);
                out.push_back(e);
            }
            break;
        }
        case NullCircle: loop(out, ngon(48, r, u, v * a, 0.0f)); break;
        case NullRectangle: loop(out, {-u * r - v * (r * a), u * r - v * (r * a), u * r + v * (r * a), -u * r + v * (r * a)}); break;
        case NullDiamond: loop(out, ngon(4, r, u, v * a, 0.0f)); break;
        case NullTriangle: loop(out, ngon(3, r, u, v * a, kPi / 2.0f)); break;
        case NullPentagon: loop(out, ngon(5, r, u, v * a, kPi / 2.0f)); break;
        case NullHexagon: loop(out, ngon(6, r, u, v * a, 0.0f)); break;
        case NullOctagon: loop(out, ngon(8, r, u, v * a, kPi / 8.0f)); break;
        case NullStar: {
            std::vector<glm::vec3> p;
            for (int i = 0; i < 10; ++i) {
                const float ang = kPi / 2.0f + kPi * (float)i / 5.0f, rr = (i % 2 == 0) ? r : r * 0.45f;
                p.push_back(u * (std::cos(ang) * rr) + v * (a * std::sin(ang) * rr));
            }
            loop(out, p);
            break;
        }
        case NullAxis:
            for (int i = 0; i < 3; ++i) {
                glm::vec3 e(0.0f);
                e[i] = r;
                out.push_back(-e);
                out.push_back(e);
            }
            break;
        case NullCube: {
            for (int i = 0; i < 8; ++i)
                for (int k = 0; k < 3; ++k)
                    if (!(i & (1 << k))) {
                        glm::vec3 p0((i & 1) ? r : -r, (i & 2) ? r : -r, (i & 4) ? r : -r), p1 = p0;
                        p1[k] = r;
                        out.push_back(p0);
                        out.push_back(p1);
                    }
            break;
        }
        case NullPyramid: {
            const glm::vec3 apex(0, r, 0);
            const glm::vec3 b[4] = {{-r, -r, -r}, {r, -r, -r}, {r, -r, r}, {-r, -r, r}};
            for (int i = 0; i < 4; ++i) {
                out.push_back(b[i]);
                out.push_back(b[(i + 1) % 4]);
                out.push_back(b[i]);
                out.push_back(apex);
            }
            break;
        }
        case NullSphere:
            loop(out, ngon(48, r, {1, 0, 0}, {0, 1, 0}, 0.0f));
            loop(out, ngon(48, r, {0, 0, 1}, {0, 1, 0}, 0.0f));
            loop(out, ngon(48, r, {1, 0, 0}, {0, 0, 1}, 0.0f));
            break;
        default: break;
    }
}

namespace {

void walk(const SceneNode& n, const glm::mat4& parent, bool parentVis, const SceneNode* selected,
          std::vector<Vertex>& lines, std::vector<glm::vec3>& tmp) {
    const glm::mat4 w = parent * n.localMatrix();
    const bool vis = resolveVisibility(n.editorVis, parentVis);
    if (vis && n.primitive == PrimitiveType::None && n.nullDisplay.shape >= 0 && n.nullDisplay.shape != NullNone) {
        tmp.clear();
        nullShapeLocalLines(n.nullDisplay, tmp);
        const glm::vec3 col = &n == selected ? glm::vec3(1.0f, 0.85f, 0.2f) : glm::vec3(0.88f, 0.9f, 0.92f);
        for (const glm::vec3& p : tmp) {
            Vertex vtx{};
            vtx.position = glm::vec3(w * glm::vec4(p, 1.0f));
            vtx.color = col;
            vtx.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            lines.push_back(vtx);
        }
    }
    for (const auto& c : n.children()) walk(*c, w, vis, selected, lines, tmp);
}

} // namespace

bool rayHitsNullShape(const SceneNode& node, const glm::mat4& world, const glm::vec3& o, const glm::vec3& d,
                      float& dist) {
    if (node.nullDisplay.shape < 0 || node.nullDisplay.shape == NullNone) return false;
    std::vector<glm::vec3> seg;
    nullShapeLocalLines(node.nullDisplay, seg);
    bool hit = false;
    float best = 1e30f;
    for (size_t i = 0; i + 1 < seg.size(); i += 2) {
        const glm::vec3 a = glm::vec3(world * glm::vec4(seg[i], 1.0f));
        const glm::vec3 b = glm::vec3(world * glm::vec4(seg[i + 1], 1.0f));
        // Punto mas cercano entre el rayo (o + t d, t >= 0) y el segmento (a + s e, 0 <= s <= 1).
        const glm::vec3 e = b - a, w0 = o - a;
        const float A = glm::dot(d, d), B = glm::dot(d, e), C = glm::dot(e, e);
        const float D = glm::dot(d, w0), E = glm::dot(e, w0);
        const float den = A * C - B * B;
        float s = den > 1e-12f ? (A * E - B * D) / den : 0.0f;
        s = glm::clamp(s, 0.0f, 1.0f);
        float t = (B * s - D) / A;
        if (t < 0.0f) { t = 0.0f; s = C > 1e-12f ? glm::clamp(E / C, 0.0f, 1.0f) : 0.0f; }
        const float gap = glm::length((o + d * t) - (a + e * s));
        const float tol = glm::max(0.01f, 0.012f * t);
        if (gap <= tol && t < best) { best = t; hit = true; }
    }
    if (hit) dist = best;
    return hit;
}

void buildNullShapes(const SceneNode& root, const SceneNode* selected, std::vector<Vertex>& lines) {
    std::vector<glm::vec3> tmp;
    walk(root, glm::mat4(1.0f), true, selected, lines, tmp);
}

} // namespace iron3d
