#include "IkSolver.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace iron3d {

namespace {

glm::vec3 worldPos(const SceneNode& n) { return glm::vec3(n.worldMatrix()[3]); }

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float l = glm::length(v);
    return l > 1e-9f ? v / l : fallback;
}

// Gira el nodo (sobre su propio origen, en mundo) con la rotacion `q`.
void rotateNodeWorld(SceneNode& n, const glm::quat& q) {
    glm::mat4 m = n.worldMatrix();
    const glm::vec4 pos = m[3];
    m[3] = glm::vec4(0, 0, 0, 1);
    m = glm::mat4_cast(q) * m;
    m[3] = pos;
    n.setLocalMatrix(glm::inverse(n.parentWorldMatrix()) * m);
}

glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if (d > 0.999999f) return glm::quat(1, 0, 0, 0);
    glm::vec3 axis = glm::cross(from, to);
    if (glm::length(axis) < 1e-6f) {
        // Opuestos: un eje perpendicular cualquiera.
        axis = glm::cross(from, std::abs(from.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
    }
    return glm::angleAxis(std::acos(d), glm::normalize(axis));
}

} // namespace

SceneNode* findNodeByPath(SceneNode& anyNode, const std::string& path) {
    if (path.empty()) return nullptr;
    SceneNode* cur = &anyNode;
    while (cur->parent()) cur = cur->parent();
    size_t a = 0;
    while (cur && a <= path.size()) {
        const size_t b = path.find('/', a);
        const std::string part = path.substr(a, b == std::string::npos ? std::string::npos : b - a);
        SceneNode* next = nullptr;
        for (const auto& c : cur->children())
            if (c->name() == part) {
                next = c.get();
                break;
            }
        cur = next;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return cur;
}

std::vector<SceneNode*> chainBetween(SceneNode& root, SceneNode& tip) {
    std::vector<SceneNode*> chain;
    for (SceneNode* n = &tip; n; n = n->parent()) {
        chain.push_back(n);
        if (n == &root) {
            std::reverse(chain.begin(), chain.end());
            return chain;
        }
    }
    return {};
}

IkResult solveIkChain(const std::vector<SceneNode*>& chain, const glm::vec3& target,
                      const std::optional<glm::vec3>& pole, int maxIterations, float tolerance) {
    IkResult res;
    const size_t n = chain.size();
    if (n < 2) return res;
    std::vector<glm::vec3> P(n);
    std::vector<float> L(n - 1);
    float total = 0.0f;
    for (size_t i = 0; i < n; ++i) P[i] = worldPos(*chain[i]);
    for (size_t i = 0; i + 1 < n; ++i) {
        L[i] = glm::length(P[i + 1] - P[i]);
        total += L[i];
    }
    const glm::vec3 base = P[0];
    // Cadena recta (sin codo): FABRIK la dejaria plegada sobre el eje. Se empuja un poco el
    // medio hacia el polo (o hacia un lado cualquiera) para que se doble por el lado correcto.
    if (n >= 3) {
        const glm::vec3 ax = safeNormalize(P[n - 1] - P[0], glm::vec3(0, 1, 0));
        float maxRad = 0.0f;
        for (size_t i = 1; i + 1 < n; ++i) {
            const glm::vec3 v = P[i] - P[0];
            maxRad = std::max(maxRad, glm::length(v - ax * glm::dot(v, ax)));
        }
        if (maxRad < 1e-3f * std::max(total, 1e-6f)) {
            glm::vec3 side(0.0f);
            if (pole) {
                const glm::vec3 v = *pole - P[0];
                side = v - ax * glm::dot(v, ax);
            }
            if (glm::length(side) < 1e-6f)
                side = glm::cross(ax, std::abs(ax.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
            side = glm::normalize(side);
            for (size_t i = 1; i + 1 < n; ++i) P[i] += side * (0.1f * total);
        }
    }
    if (glm::length(target - base) >= total) {
        // Fuera de alcance: cadena recta hacia el objetivo.
        const glm::vec3 dir = safeNormalize(target - base, glm::vec3(0, 1, 0));
        for (size_t i = 0; i + 1 < n; ++i) P[i + 1] = P[i] + dir * L[i];
    } else {
        for (int it = 0; it < maxIterations; ++it) {
            res.iterations = it + 1;
            if (glm::length(P[n - 1] - target) <= tolerance * 0.1f) break;
            P[n - 1] = target;
            for (size_t i = n - 1; i-- > 0;) P[i] = P[i + 1] + safeNormalize(P[i] - P[i + 1], glm::vec3(0, 1, 0)) * L[i];
            P[0] = base;
            for (size_t i = 0; i + 1 < n; ++i) P[i + 1] = P[i] + safeNormalize(P[i + 1] - P[i], glm::vec3(0, 1, 0)) * L[i];
        }
    }
    // Polo: giro rigido de las articulaciones intermedias alrededor del eje raiz->punta.
    if (pole && n >= 3) {
        const glm::vec3 axis = safeNormalize(P[n - 1] - P[0], glm::vec3(0, 1, 0));
        auto radialOf = [&](const glm::vec3& p) {
            const glm::vec3 v = p - P[0];
            return v - axis * glm::dot(v, axis);
        };
        const glm::vec3 poleRad = radialOf(*pole);
        glm::vec3 bestRad(0.0f);
        for (size_t i = 1; i + 1 < n; ++i) {
            const glm::vec3 r = radialOf(P[i]);
            if (glm::length(r) > glm::length(bestRad)) bestRad = r;
        }
        if (glm::length(poleRad) > 1e-6f && glm::length(bestRad) > 1e-6f) {
            const float ang = std::atan2(glm::dot(glm::cross(bestRad, poleRad), axis), glm::dot(bestRad, poleRad));
            const glm::quat q = glm::angleAxis(ang, axis);
            for (size_t i = 1; i + 1 < n; ++i) P[i] = P[0] + q * (P[i] - P[0]);
        }
    }
    // Girar cada articulacion para que apunte a la siguiente posicion.
    for (size_t i = 0; i + 1 < n; ++i) {
        const glm::vec3 cur = safeNormalize(worldPos(*chain[i + 1]) - worldPos(*chain[i]), glm::vec3(0, 1, 0));
        const glm::vec3 des = safeNormalize(P[i + 1] - P[i], cur);
        rotateNodeWorld(*chain[i], rotationBetween(cur, des));
    }
    res.error = glm::length(worldPos(*chain[n - 1]) - target);
    res.solved = res.error <= std::max(tolerance, 1e-3f);
    return res;
}

namespace {

void collectIk(SceneNode& n, std::vector<SceneNode*>& out) {
    if (n.ik.present && n.ik.enabled && !n.ik.tip.empty() && !n.ik.target.empty()) out.push_back(&n);
    for (const auto& c : n.children()) collectIk(*c, out);
}

} // namespace

int applyJointLimits(SceneNode& node) {
    int clamped = 0;
    const JointLimits& l = node.jointLimits;
    if (l.present) {
        // rotationDeg: x = P, y = H, z = B; los limites van en orden H, P, B.
        float* axis[3] = {&node.rotationDeg.y, &node.rotationDeg.x, &node.rotationDeg.z};
        for (int i = 0; i < 3; ++i) {
            if (!l.use[i]) continue;
            const float lo = std::min(l.minDeg[i], l.maxDeg[i]), hi = std::max(l.minDeg[i], l.maxDeg[i]);
            const float v = *axis[i], c = std::clamp(v, lo, hi);
            if (c != v) {
                *axis[i] = v + (c - v) * std::clamp(l.strength, 0.0f, 1.0f);
                ++clamped;
            }
        }
    }
    for (const auto& c : node.children()) clamped += applyJointLimits(*c);
    return clamped;
}

int solveAllIk(SceneNode& root) {
    std::vector<SceneNode*> tagged;
    collectIk(root, tagged);
    int solved = 0;
    for (SceneNode* n : tagged) {
        SceneNode* tip = findNodeByPath(*n, n->ik.tip);
        SceneNode* goal = findNodeByPath(*n, n->ik.target);
        if (!tip || !goal) continue;
        const std::vector<SceneNode*> chain = chainBetween(*n, *tip);
        if (chain.size() < 2) continue;
        std::optional<glm::vec3> pole;
        if (SceneNode* p = findNodeByPath(*n, n->ik.pole)) pole = worldPos(*p);
        const bool ok = solveIkChain(chain, worldPos(*goal), pole).solved;
        for (SceneNode* j : chain) applyJointLimits(*j);
        if (ok) ++solved;
    }
    return solved;
}

} // namespace iron3d
