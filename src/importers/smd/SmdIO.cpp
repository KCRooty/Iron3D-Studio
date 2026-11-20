#include "SmdIO.h"

#include "core/MeshData.h"

#include <algorithm>
#include <cctype>
#include <fstream>

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

struct Ctx {
    std::ofstream out;
    const std::vector<Material>* mats = nullptr;
    SmdReport* rep = nullptr;
    float scale = 0.525f;
};

constexpr float kCmPerUnit = 200.0f;  // 1 unidad de Iron3D = 200 cm (como en MainUi)

glm::vec3 toSource(const glm::vec3& p) { return glm::vec3(p.x, -p.z, p.y); }

void writeNode(Ctx& cx, const SceneNode& n) {
    const MeshData mesh = meshOf(n);
    if (!mesh.vertices.empty() && mesh.indices.size() >= 3) {
        const glm::mat4 w = n.worldMatrix();
        const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(w)));
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
                cx.out << "0  " << p.x << " " << p.y << " " << p.z << "  " << nn.x << " " << nn.y << " " << nn.z << "  "
                       << v.uv.x << " " << (1.0f - v.uv.y) << "\n";
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
    cx.out.open(path);
    if (!cx.out) {
        if (error) *error = "No se pudo crear el archivo.";
        return false;
    }
    cx.out.precision(6);
    cx.out << "version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0  0 0 0  0 0 0\nend\ntriangles\n";
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
