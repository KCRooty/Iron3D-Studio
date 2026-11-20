#include "SmdIO.h"

#include "core/MeshData.h"
#include "core/Skin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>

namespace iron3d {

namespace fs = std::filesystem;

namespace {

std::string cleanName(std::string s) {
    for (char& c : s) {
        const unsigned char u = (unsigned char)c;
        if (std::isalnum(u) || c == '_' || c == '-' || c == '/') c = (char)std::tolower(u);
        else c = '_';
    }
    return s.empty() ? std::string("material") : s;
}

MeshData meshOf(const SceneNode& n) {
    if (n.mesh) return *n.mesh;
    switch (n.primitive) {
        case PrimitiveType::Cube: return makeCubeMesh();
        case PrimitiveType::Sphere: return makeSphereMesh();
        case PrimitiveType::Plane: return makePlaneMesh();
        default: break;
    }
    return {};
}

constexpr float kCmPerUnit = 200.0f;  // 1 unidad de Iron3D = 200 cm (como en MainUi)

glm::vec3 toSource(const glm::vec3& p) { return glm::vec3(p.x, -p.z, p.y); }

// Matriz de cambio de ejes Iron3D (Y arriba) -> Source (Z arriba): (x, y, z) -> (x, -z, y).
glm::mat3 axisSwap() {
    return glm::mat3(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));  // columnas
}

// Pose rigida (sin escala) de un mundo de Iron3D, ya en ejes y unidades de Source.
glm::mat4 sourcePose(const glm::mat4& w, float scale) {
    glm::mat3 lin(w);
    for (int c = 0; c < 3; ++c) {
        const float l = glm::length(lin[c]);
        if (l > 1e-8f) lin[c] /= l;
    }
    const glm::mat3 s = axisSwap();
    const glm::mat3 r = s * lin * glm::transpose(s);
    glm::mat4 m(r);
    m[3] = glm::vec4(toSource(glm::vec3(w[3])) * (scale * kCmPerUnit), 1.0f);
    return m;
}

// Euler de studiomdl (radianes, R = Rz(z) * Ry(y) * Rx(x)) a partir de una rotacion.
glm::vec3 sourceEuler(const glm::mat4& m) {
    // glm es columna-fila: m[col][row]. R(fila, col) = m[col][fila].
    const float r20 = m[0][2], r21 = m[1][2], r22 = m[2][2], r10 = m[0][1], r00 = m[0][0];
    const float y = std::asin(std::clamp(-r20, -1.0f, 1.0f));
    if (std::fabs(r20) > 0.99999f) {  // bloqueo de cardan: se fija el giro X en 0
        return glm::vec3(0.0f, y, std::atan2(-m[1][0], m[1][1]));
    }
    return glm::vec3(std::atan2(r21, r22), y, std::atan2(r10, r00));
}

struct Bone {
    const SceneNode* node = nullptr;  // nullptr = "root"
    int parent = -1;
    std::string name;
};

struct Ctx {
    std::ofstream out;
    const std::vector<Material>* mats = nullptr;
    SmdReport* rep = nullptr;
    float scale = 0.525f;
    std::vector<Bone> bones;
    std::map<const SceneNode*, int> boneOf;
};

void collectJoints(Ctx& cx, const SceneNode& n) {
    if (n.isJoint) {
        int parent = 0;
        for (const SceneNode* a = n.parent(); a; a = a->parent()) {
            auto it = cx.boneOf.find(a);
            if (it != cx.boneOf.end()) { parent = it->second; break; }
        }
        std::string name = cleanName(n.name());
        int dup = 0;
        for (const Bone& b : cx.bones)
            if (b.name == name) ++dup;
        if (dup) name += "_" + std::to_string(dup + 1);
        cx.boneOf[&n] = (int)cx.bones.size();
        cx.bones.push_back({&n, parent, name});
    }
    for (const auto& c : n.children()) collectJoints(cx, *c);
}

// Hueso rigido de un nodo: el de la articulacion mas cercana por encima (o el propio), si no 0.
int rigidBone(const Ctx& cx, const SceneNode& n) {
    for (const SceneNode* a = &n; a; a = a->parent()) {
        auto it = cx.boneOf.find(a);
        if (it != cx.boneOf.end()) return it->second;
    }
    return 0;
}

const SceneNode* byPath(const SceneNode& any, const std::string& path) {
    if (path.empty()) return nullptr;
    const SceneNode* cur = &any;
    while (cur->parent()) cur = cur->parent();
    size_t a = 0;
    while (cur && a <= path.size()) {
        const size_t b = path.find('/', a);
        const std::string part = path.substr(a, b == std::string::npos ? std::string::npos : b - a);
        const SceneNode* next = nullptr;
        for (const auto& c : cur->children())
            if (c->name() == part) { next = c.get(); break; }
        cur = next;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return cur;
}

void writeSkeleton(Ctx& cx) {
    cx.out << "nodes\n";
    for (size_t i = 0; i < cx.bones.size(); ++i)
        cx.out << i << " \"" << cx.bones[i].name << "\" " << cx.bones[i].parent << "\n";
    cx.out << "end\nskeleton\ntime 0\n";
    std::vector<glm::mat4> world(cx.bones.size(), glm::mat4(1.0f));
    for (size_t i = 1; i < cx.bones.size(); ++i) world[i] = sourcePose(cx.bones[i].node->worldMatrix(), cx.scale);
    for (size_t i = 0; i < cx.bones.size(); ++i) {
        glm::mat4 local = world[i];
        if (cx.bones[i].parent >= 0) local = glm::inverse(world[(size_t)cx.bones[i].parent]) * world[i];
        const glm::vec3 e = sourceEuler(local);
        cx.out << i << "  " << local[3].x << " " << local[3].y << " " << local[3].z << "  " << e.x << " " << e.y << " "
               << e.z << "\n";
    }
    cx.out << "end\n";
}

void writeNode(Ctx& cx, const SceneNode& n) {
    const MeshData mesh = meshOf(n);
    if (!mesh.vertices.empty() && mesh.indices.size() >= 3) {
        const glm::mat4 w = n.worldMatrix();
        const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(w)));

        // Pesos por vertice: de la Piel si sus articulaciones estan en la exportacion; si no, rigido.
        struct Link { int bone; float weight; };
        std::vector<std::vector<Link>> links;
        const int rigid = rigidBone(cx, n);
        if (n.skin && n.skin->influences.size() == mesh.vertices.size()) {
            std::vector<int> boneOfJoint;
            bool ok = true;
            for (const std::string& p : n.skin->jointPaths) {
                const SceneNode* j = byPath(n, p);
                auto it = j ? cx.boneOf.find(j) : cx.boneOf.end();
                if (it == cx.boneOf.end()) { ok = false; break; }
                boneOfJoint.push_back(it->second);
            }
            if (ok) {
                links.resize(mesh.vertices.size());
                bool cut = false;
                for (size_t v = 0; v < links.size(); ++v) {
                    const SkinInfluence& si = n.skin->influences[v];
                    float sum = 0;
                    for (int k = 0; k < 4; ++k) {
                        if (si.joint[k] < 0 || (size_t)si.joint[k] >= boneOfJoint.size() || si.weight[k] <= 0) continue;
                        links[v].push_back({boneOfJoint[(size_t)si.joint[k]], si.weight[k]});
                        sum += si.weight[k];
                    }
                    if (sum < 0.999f) links[v].push_back({rigid, 1.0f - sum});  // el resto sigue rigido con la malla
                    std::sort(links[v].begin(), links[v].end(), [](const Link& a, const Link& b) { return a.weight > b.weight; });
                    if (links[v].size() > 3) {  // studiomdl admite 3 huesos por vertice
                        links[v].resize(3);
                        cut = true;
                    }
                    float s2 = 0;
                    for (const Link& l : links[v]) s2 += l.weight;
                    for (Link& l : links[v]) l.weight = s2 > 0 ? l.weight / s2 : 1.0f;
                }
                if (cut) cx.rep->warnings.push_back("\"" + n.name() + "\": vertices con mas de 3 huesos; se conservan los 3 de mas peso.");
                ++cx.rep->skinnedMeshes;
            }
        }

        auto matOfTri = [&](size_t tri) {
            const uint32_t first = (uint32_t)(tri * 3);
            for (const MaterialPart& p : n.materialParts)
                if (first >= p.firstIndex && first < p.firstIndex + p.indexCount) return p.materialIndex;
            return n.materialIndex;
        };
        for (size_t tri = 0; tri < mesh.indices.size() / 3; ++tri) {
            const int mi = matOfTri(tri);
            std::string name = "no_material";
            if (mi >= 0 && (size_t)mi < cx.mats->size()) name = smdMaterialName((*cx.mats)[(size_t)mi]);
            if (std::find(cx.rep->materials.begin(), cx.rep->materials.end(), name) == cx.rep->materials.end())
                cx.rep->materials.push_back(name);
            cx.out << name << "\n";
            const uint32_t ids[3] = {mesh.indices[tri * 3], mesh.indices[tri * 3 + 2], mesh.indices[tri * 3 + 1]};
            for (uint32_t i : ids) {
                if (i >= mesh.vertices.size()) continue;
                const Vertex& v = mesh.vertices[i];
                const glm::vec3 p = toSource(glm::vec3(w * glm::vec4(v.position, 1.0f))) * (cx.scale * kCmPerUnit);
                glm::vec3 nn = nm * v.normal;
                nn = glm::dot(nn, nn) > 0 ? toSource(glm::normalize(nn)) : glm::vec3(0, 0, 1);
                const int main = links.empty() || links[i].empty() ? rigid : links[i][0].bone;
                cx.out << main << "  " << p.x << " " << p.y << " " << p.z << "  " << nn.x << " " << nn.y << " " << nn.z
                       << "  " << v.uv.x << " " << (1.0f - v.uv.y);
                if (!links.empty() && !links[i].empty()) {
                    cx.out << "  " << links[i].size();
                    for (const Link& l : links[i]) cx.out << " " << l.bone << " " << l.weight;
                }
                cx.out << "\n";
            }
            ++cx.rep->triangles;
        }
        ++cx.rep->objects;
    }
    for (const auto& c : n.children()) writeNode(cx, *c);
}

}  // namespace

std::string smdMaterialName(const Material& m) {
    const std::string& tex = m.textures[(int)TexSlot::Color];
    if (!tex.empty()) {
        const std::string stem = fs::path(tex).stem().string();
        if (!stem.empty()) return cleanName(stem);
    }
    return cleanName(m.name);
}

bool exportSmd(const fs::path& path, const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
               SmdReport& report, std::string* error, float unitsPerCm) {
    Ctx cx;
    cx.mats = &materials;
    cx.rep = &report;
    cx.scale = unitsPerCm;
    cx.bones.push_back({nullptr, -1, "root"});
    for (const SceneNode* r : roots) collectJoints(cx, *r);
    report.bones = (int)cx.bones.size();
    cx.out.open(path);
    if (!cx.out) {
        if (error) *error = "No se pudo crear el archivo.";
        return false;
    }
    cx.out.precision(6);
    cx.out << "version 1\n";
    writeSkeleton(cx);
    cx.out << "triangles\n";
    for (const SceneNode* r : roots) writeNode(cx, *r);
    cx.out << "end\n";
    cx.out.close();
    if (report.objects == 0) {
        if (error) *error = "No hay objetos con malla que exportar.";
        return false;
    }
    return true;
}

}  // namespace iron3d
