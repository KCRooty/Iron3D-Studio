#include "LightGizmo.h"

#include <algorithm>
#include <cmath>

namespace iron3d {

namespace {

constexpr float kPi = 3.14159265358979f;

void seg(std::vector<glm::vec3>& out, const glm::vec3& a, const glm::vec3& b) {
    out.push_back(a);
    out.push_back(b);
}

// Poligono cerrado en el plano XY a la altura z.
void ring(std::vector<glm::vec3>& out, float rx, float ry, float z, int n) {
    for (int i = 0; i < n; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)n, a1 = 2.0f * kPi * (float)(i + 1) / (float)n;
        seg(out, {std::cos(a0) * rx, std::sin(a0) * ry, z}, {std::cos(a1) * rx, std::sin(a1) * ry, z});
    }
}

void rect(std::vector<glm::vec3>& out, float hx, float hy, float z) {
    const glm::vec3 p0(-hx, -hy, z), p1(hx, -hy, z), p2(hx, hy, z), p3(-hx, hy, z);
    seg(out, p0, p1);
    seg(out, p1, p2);
    seg(out, p2, p3);
    seg(out, p3, p0);
}

bool isSpotSquare(LightKind k) {
    return k == LightKind::SpotSquare || k == LightKind::ParallelSpotSquare;
}
bool isParallel(LightKind k) {
    return k == LightKind::Parallel || k == LightKind::ParallelSpot || k == LightKind::ParallelSpotSquare;
}

glm::mat4 noScale(const glm::mat4& w) {
    glm::mat4 m = w;
    for (int c = 0; c < 3; ++c) {
        const float len = glm::length(glm::vec3(m[c]));
        if (len > 1e-8f) m[c] = glm::vec4(glm::vec3(m[c]) / len, 0.0f);
    }
    return m;
}

void walk(const SceneNode& n, const glm::mat4& parent, bool parentVis, const SceneNode* selected,
          std::vector<Vertex>& lines, std::vector<glm::vec3>& tmp) {
    const glm::mat4 w = parent * n.localMatrix();
    const bool vis = resolveVisibility(n.editorVis, parentVis);
    if (vis && n.primitive == PrimitiveType::Light) {
        // Posicion y giro de la luz, sin su escala (una luz no es geometria escalable).
        const glm::mat4 m = noScale(w);
        tmp.clear();
        lightStarLines(kLightStarRadius, tmp);
        lightGizmoLocalLines(n.light, tmp);
        const glm::vec3 col = &n == selected ? glm::vec3(1.0f, 0.85f, 0.2f) : glm::vec3(1.0f);
        for (const glm::vec3& p : tmp) {
            Vertex v{};
            v.position = glm::vec3(m * glm::vec4(p, 1.0f));
            v.color = col;
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            lines.push_back(v);
        }
    }
    for (const auto& c : n.children()) walk(*c, w, vis, selected, lines, tmp);
}

} // namespace

void lightStarLines(float r, std::vector<glm::vec3>& out) {
    // Estrella de C4D: rayos que salen del centro hacia los ejes y las diagonales, de largo alterno.
    const float d = 0.57735f; // 1/sqrt(3)
    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& a : axes) seg(out, a * (r * 0.12f), a * r);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 a((i & 1) ? d : -d, (i & 2) ? d : -d, (i & 4) ? d : -d);
        seg(out, a * (r * 0.12f), a * (r * 0.72f));
    }
}

void lightGizmoLocalLines(const LightData& l, std::vector<glm::vec3>& out) {
    constexpr float kLen = 1.25f; // largo del cono / linea (unidades de escena)
    switch (l.kind) {
        case LightKind::Spot:
        case LightKind::SpotSquare:
        case LightKind::ParallelSpot:
        case LightKind::ParallelSpotSquare: {
            const float half = glm::radians(glm::clamp(l.outerAngleDeg, 1.0f, 170.0f) * 0.5f);
            const bool par = isParallel(l.kind);
            const float rEnd = std::min(std::tan(half) * kLen, 6.0f);
            const float rStart = par ? rEnd : 0.0f;
            seg(out, {0, 0, 0}, {0, 0, kLen}); // eje central
            if (isSpotSquare(l.kind)) {
                rect(out, rEnd, rEnd, kLen);
                if (par) rect(out, rStart, rStart, 0.0f);
                for (int i = 0; i < 4; ++i) {
                    const float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
                    seg(out, {sx * rStart, sy * rStart, 0.0f}, {sx * rEnd, sy * rEnd, kLen});
                }
            } else {
                ring(out, rEnd, rEnd, kLen, 48);
                if (par) ring(out, rStart, rStart, 0.0f, 48);
                for (int i = 0; i < 4; ++i) {
                    const float a = kPi * 0.5f * (float)i;
                    seg(out, {std::cos(a) * rStart, std::sin(a) * rStart, 0.0f}, {std::cos(a) * rEnd, std::sin(a) * rEnd, kLen});
                }
            }
            break;
        }
        case LightKind::Infinite:
        case LightKind::Parallel:
            // Una flecha hacia +Z: la luz solo tiene direccion, no posicion de cono.
            seg(out, {0, 0, 0}, {0, 0, kLen});
            seg(out, {0, 0, kLen}, {0.06f, 0, kLen - 0.15f});
            seg(out, {0, 0, kLen}, {-0.06f, 0, kLen - 0.15f});
            seg(out, {0, 0, kLen}, {0, 0.06f, kLen - 0.15f});
            seg(out, {0, 0, kLen}, {0, -0.06f, kLen - 0.15f});
            break;
        case LightKind::Area: {
            const float hx = std::max(l.areaSize.x, 0.02f) * 0.5f, hy = std::max(l.areaSize.y, 0.02f) * 0.5f;
            if (l.areaShape == 0) ring(out, hx, hy, 0.0f, 48); // disco
            else rect(out, hx, hy, 0.0f);
            seg(out, {0, 0, 0}, {0, 0, 0.5f}); // hacia donde ilumina
            break;
        }
        case LightKind::Tube: {
            const float hx = std::max(l.areaSize.x, 0.02f) * 0.5f;
            seg(out, {-hx, 0, 0}, {hx, 0, 0});
            break;
        }
        default: break; // Omni y Fotometrica: solo la estrella
    }
}

void buildLightGizmos(const SceneNode& root, const SceneNode* selected, std::vector<Vertex>& lines) {
    std::vector<glm::vec3> tmp;
    walk(root, glm::mat4(1.0f), true, selected, lines, tmp);
}

} // namespace iron3d
