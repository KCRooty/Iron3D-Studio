#include "Symmetry.h"

#include <algorithm>
#include <cmath>

namespace iron3d {

MeshData mirrorMeshData(const MeshData& src, int axis, const glm::mat4& toParent, float tol) {
    const int a = std::clamp(axis, 0, 2);
    MeshData out;
    out.vertices = src.vertices;
    const glm::mat3 nm = glm::mat3(toParent);
    for (Vertex& v : out.vertices) {
        glm::vec3 p = glm::vec3(toParent * glm::vec4(v.position, 1.0f));
        if (std::fabs(p[a]) <= tol) p[a] = 0.0f;
        p[a] = -p[a];
        glm::vec3 n = nm * v.normal;
        if (glm::length(n) > 1e-12f) n = glm::normalize(n);
        n[a] = -n[a];
        v.position = p;
        v.normal = n;
    }
    out.indices = src.indices;
    // El reflejo invierte el giro: cada triangulo (a, b, c) pasa a (a, c, b).
    for (size_t i = 0; i + 2 < out.indices.size(); i += 3) std::swap(out.indices[i + 1], out.indices[i + 2]);
    out.faceTris = src.faceTris;
    updateBounds(out);
    return out;
}

namespace {
SceneNode* firstMeshChild(SceneNode& node) {
    for (const auto& c : node.children())
        if (c->mesh && !c->symmetry.present) return c.get();
    return nullptr;
}

void walk(SceneNode& node, int& count) {
    if (node.symmetry.present) {
        SymmetryData& s = node.symmetry;
        bakeParametricChildren(node);
        if (SceneNode* child = firstMeshChild(node)) {
            glm::mat4 cm = child->localMatrix();
            const bool same = s.lastSource == child->mesh && s.lastChildMatrix == cm && s.lastAxis == s.axis &&
                              s.lastTolCm == s.tolCm && node.mesh == s.output;
            if (!same) {
                auto m = std::make_shared<const MeshData>(mirrorMeshData(*child->mesh, s.axis, cm, s.tolCm / 200.0f));
                node.mesh = m;
                node.primitive = PrimitiveType::Mesh;
                node.materialIndex = child->materialIndex;
                node.texTag = child->texTag;
                s.output = m;
                s.lastSource = child->mesh;
                s.lastChildMatrix = cm;
                s.lastAxis = s.axis;
                s.lastTolCm = s.tolCm;
                ++count;
            }
        } else if (node.mesh) {
            node.mesh.reset();
            s.output.reset();
            s.lastSource.reset();
        }
    }
    for (const auto& c : node.children()) walk(*c, count);
}
} // namespace

int bakeParametricChildren(SceneNode& node) {
    int n = 0;
    for (const auto& c : node.children()) {
        if (c->mesh) continue;
        MeshData m;
        switch (c->primitive) {
            case PrimitiveType::Cube: m = makeCubeMesh(); break;
            case PrimitiveType::Sphere: m = makeSphereMesh(); break;
            case PrimitiveType::Plane: m = makePlaneMesh(); break;
            default: continue;
        }
        updateBounds(m);
        c->mesh = std::make_shared<const MeshData>(std::move(m));
        c->primitive = PrimitiveType::Mesh;
        ++n;
    }
    return n;
}

int evaluateSymmetry(SceneNode& root) {
    int n = 0;
    walk(root, n);
    return n;
}

} // namespace iron3d
