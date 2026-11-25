#include "importers/fbx/FbxIO.h"

#include "core/MeshData.h"
#include "ufbx.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace iron3d {

namespace {

glm::mat4 toGlm(const ufbx_matrix& m) {
    glm::mat4 r(1.0f);
    r[0] = glm::vec4((float)m.m00, (float)m.m10, (float)m.m20, 0.0f);
    r[1] = glm::vec4((float)m.m01, (float)m.m11, (float)m.m21, 0.0f);
    r[2] = glm::vec4((float)m.m02, (float)m.m12, (float)m.m22, 0.0f);
    r[3] = glm::vec4((float)m.m03, (float)m.m13, (float)m.m23, 1.0f);
    return r;
}

bool isIdentity(const ufbx_matrix& m) {
    const ufbx_matrix id = ufbx_identity_matrix;
    const double* a = &m.m00;
    const double* b = &id.m00;
    for (int i = 0; i < 12; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-9) return false;
    return true;
}

struct ImportCtx {
    std::vector<Material>* materials = nullptr;
    FbxReport* rep = nullptr;
    std::map<const ufbx_material*, int> matMap;
    std::string baseDir;
};

int materialFor(ImportCtx& cx, const ufbx_material* m) {
    if (!m) return -1;
    auto it = cx.matMap.find(m);
    if (it != cx.matMap.end()) return it->second;
    Material out;
    out.name = m->name.length ? std::string(m->name.data, m->name.length) : "Material";
    out.texBaseDir = cx.baseDir;
    if (m->pbr.base_color.has_value)
        out.baseColor = {(float)m->pbr.base_color.value_vec3.x, (float)m->pbr.base_color.value_vec3.y,
                         (float)m->pbr.base_color.value_vec3.z};
    out.metallic = m->pbr.metalness.has_value ? (float)m->pbr.metalness.value_real : 0.0f;
    out.roughness = m->pbr.roughness.has_value ? (float)m->pbr.roughness.value_real : 0.5f;
    if (m->pbr.emission_color.has_value) {
        const ufbx_vec3 e = m->pbr.emission_color.value_vec3;
        const float k = m->pbr.emission_factor.has_value ? (float)m->pbr.emission_factor.value_real : 1.0f;
        out.emission = glm::vec3((float)e.x, (float)e.y, (float)e.z) * k;
        if (out.emission.x + out.emission.y + out.emission.z > 1e-4f) out.useLuminance = true;
    }
    if (const ufbx_texture* t = m->pbr.base_color.texture) {
        const ufbx_string& f = t->absolute_filename.length && fs::exists(fs::path(std::string(t->absolute_filename.data, t->absolute_filename.length)))
                                   ? t->absolute_filename
                                   : t->filename;
        if (f.length) out.textures[(int)TexSlot::Color] = std::string(f.data, f.length);
    }
    const int idx = (int)cx.materials->size();
    cx.materials->push_back(std::move(out));
    cx.matMap[m] = idx;
    ++cx.rep->materials;
    return idx;
}

void buildMesh(ImportCtx& cx, const ufbx_node& un, const ufbx_mesh& um, SceneNode& node) {
    auto mesh = std::make_shared<MeshData>();
    const bool geo = !isIdentity(un.geometry_to_node);
    const glm::mat4 gm = geo ? toGlm(un.geometry_to_node) : glm::mat4(1.0f);
    const glm::mat3 gn = glm::mat3(gm);
    const size_t nmat = std::max<size_t>(1, um.material_parts.count);
    std::vector<MaterialPart> parts;
    std::vector<uint32_t> tri((size_t)um.max_face_triangles * 3 + 3);
    for (size_t pi = 0; pi < nmat; ++pi) {
        MaterialPart part;
        part.firstIndex = (uint32_t)mesh->indices.size();
        int matIdx = -1;
        if (um.material_parts.count > pi) {
            const ufbx_mesh_part& mp = um.material_parts.data[pi];
            if (mp.num_triangles == 0) continue;
            if (mp.index < um.materials.count) matIdx = materialFor(cx, um.materials.data[mp.index]);
            for (size_t fi = 0; fi < mp.face_indices.count; ++fi) {
                const ufbx_face face = um.faces.data[mp.face_indices.data[fi]];
                if (face.num_indices < 3) continue;
                const uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), &um, face);
                for (uint32_t t = 0; t < nt * 3; ++t) {
                    const uint32_t c = tri[t];
                    Vertex v{};
                    const ufbx_vec3 p = ufbx_get_vertex_vec3(&um.vertex_position, c);
                    v.position = glm::vec3((float)p.x, (float)p.y, (float)p.z);
                    if (geo) v.position = glm::vec3(gm * glm::vec4(v.position, 1.0f));
                    if (um.vertex_normal.exists) {
                        const ufbx_vec3 n = ufbx_get_vertex_vec3(&um.vertex_normal, c);
                        v.normal = glm::vec3((float)n.x, (float)n.y, (float)n.z);
                        if (geo) v.normal = gn * v.normal;
                        v.normal = glm::dot(v.normal, v.normal) > 1e-20f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
                    }
                    if (um.vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&um.vertex_uv, c);
                        v.uv = glm::vec2((float)uv.x, 1.0f - (float)uv.y); // FBX: V hacia arriba
                    }
                    mesh->indices.push_back((uint32_t)mesh->vertices.size());
                    mesh->vertices.push_back(v);
                }
                for (uint32_t t = 0; t < nt; ++t) mesh->faceTris.push_back(1);
            }
        }
        part.indexCount = (uint32_t)mesh->indices.size() - part.firstIndex;
        part.materialIndex = matIdx;
        if (part.indexCount) parts.push_back(part);
    }
    if (mesh->indices.empty()) return;
    // Sin normales en el archivo: se calculan planas por triangulo.
    if (!um.vertex_normal.exists) {
        for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            const glm::vec3 a = mesh->vertices[mesh->indices[i]].position, b = mesh->vertices[mesh->indices[i + 1]].position,
                            c = mesh->vertices[mesh->indices[i + 2]].position;
            glm::vec3 n = glm::cross(b - a, c - a);
            n = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : glm::vec3(0, 1, 0);
            for (int k = 0; k < 3; ++k) mesh->vertices[mesh->indices[i + (size_t)k]].normal = n;
        }
    }
    updateBounds(*mesh);
    cx.rep->points += (long long)mesh->vertices.size();
    cx.rep->polygons += (long long)(mesh->indices.size() / 3);
    ++cx.rep->meshes;
    node.primitive = PrimitiveType::Mesh;
    node.materialIndex = parts.front().materialIndex;
    if (parts.size() > 1) node.materialParts = parts;
    node.mesh = std::move(mesh);
}

void importNode(ImportCtx& cx, const ufbx_node* un, SceneNode& parent, int depth) {
    if (depth > 200) return;
    SceneNode* node = parent.addChild(un->name.length ? std::string(un->name.data, un->name.length) : "Nodo");
    node->setLocalMatrix(toGlm(un->node_to_parent));
    ++cx.rep->nodes;
    if (un->mesh) buildMesh(cx, *un, *un->mesh, *node);
    for (size_t i = 0; i < un->children.count; ++i) importNode(cx, un->children.data[i], *node, depth + 1);
}

} // namespace

bool isFbxPath(const fs::path& path) {
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e == ".fbx";
}

bool importFbx(const fs::path& path, SceneNode& root, std::vector<Material>& materials, FbxReport& report,
               std::string* error) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.generate_missing_normals = false;
    ufbx_error err;
    const std::string p = path.string();
    ufbx_scene* scene = ufbx_load_file(p.c_str(), &opts, &err);
    if (!scene) {
        if (error) {
            char buf[512];
            ufbx_format_error(buf, sizeof(buf), &err);
            *error = buf;
        }
        return false;
    }
    ImportCtx cx;
    cx.materials = &materials;
    cx.rep = &report;
    cx.baseDir = path.parent_path().string();
    // El nodo raiz de ufbx es un nulo sin nombre: se cuelgan sus hijos directamente.
    for (size_t i = 0; i < scene->root_node->children.count; ++i)
        importNode(cx, scene->root_node->children.data[i], root, 0);
    if (scene->anim_stacks.count)
        report.warnings.push_back("El FBX trae animación o esqueleto: todavía no se importa (solo geometría y materiales).");
    ufbx_free_scene(scene);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Exportar FBX ASCII 7.4
// ---------------------------------------------------------------------------------------------
namespace {

struct ExpCtx {
    const std::vector<Material>* mats = nullptr;
    FbxReport* rep = nullptr;
    long long nextId = 100000;
    std::string objects, conns;
    std::map<int, long long> matIds;
    int modelCount = 0, geomCount = 0;
};

std::string num(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.9g", v);
    return b;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) o += (c == '"' || c == '\\') ? '_' : c;
    return o + "\"";
}

MeshData meshOfNode(const SceneNode& n) {
    if (n.mesh) return *n.mesh;
    switch (n.primitive) {
        case PrimitiveType::Cube: return makeCubeMesh();
        case PrimitiveType::Sphere: return makeSphereMesh();
        case PrimitiveType::Plane: return makePlaneMesh();
        default: return {};
    }
}

long long materialId(ExpCtx& cx, int idx) {
    auto it = cx.matIds.find(idx);
    if (it != cx.matIds.end()) return it->second;
    const long long id = cx.nextId++;
    Material m;
    if (idx >= 0 && (size_t)idx < cx.mats->size()) m = (*cx.mats)[(size_t)idx];
    std::ostringstream o;
    o << "\tMaterial: " << id << ", \"Material::" << quote(m.name).substr(1, quote(m.name).size() - 2) << "\", \"\" {\n"
      << "\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n\t\tMultiLayer: 0\n\t\tProperties70:  {\n"
      << "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\"," << num(m.baseColor.x) << "," << num(m.baseColor.y) << "," << num(m.baseColor.z) << "\n"
      << "\t\t\tP: \"EmissiveColor\", \"Color\", \"\", \"A\"," << num(m.emission.x) << "," << num(m.emission.y) << "," << num(m.emission.z) << "\n"
      << "\t\t\tP: \"Shininess\", \"Number\", \"\", \"A\"," << num((1.0f - m.roughness) * 100.0f) << "\n"
      << "\t\t}\n\t}\n";
    cx.objects += o.str();
    cx.matIds[idx] = id;
    ++cx.rep->materials;
    return id;
}

void exportNode(ExpCtx& cx, const SceneNode& n, long long parentId) {
    const MeshData mesh = meshOfNode(n);
    const bool hasMesh = !mesh.vertices.empty() && mesh.indices.size() >= 3;
    const long long modelId = cx.nextId++;
    // Transformacion local: traslacion, rotacion Euler XYZ (grados) y escala.
    const glm::mat4 L = n.localMatrix();
    const glm::vec3 t(L[3]);
    glm::vec3 s(glm::length(glm::vec3(L[0])), glm::length(glm::vec3(L[1])), glm::length(glm::vec3(L[2])));
    if (s.x < 1e-8f) s.x = 1.0f;
    if (s.y < 1e-8f) s.y = 1.0f;
    if (s.z < 1e-8f) s.z = 1.0f;
    const glm::vec3 c0 = glm::vec3(L[0]) / s.x, c1 = glm::vec3(L[1]) / s.y, c2 = glm::vec3(L[2]) / s.z;
    // M(fila,columna): M20 = c0.z, M21 = c1.z, M22 = c2.z, M10 = c0.y, M00 = c0.x, M11 = c1.y, M12 = c2.y
    float a, b, c;
    b = std::asin(std::clamp(-c0.z, -1.0f, 1.0f));
    if (std::fabs(c0.z) < 0.999999f) {
        a = std::atan2(c1.z, c2.z);
        c = std::atan2(c0.y, c0.x);
    } else {
        a = std::atan2(-c2.y, c1.y);
        c = 0.0f;
    }
    const float deg = 57.29577951f;
    std::ostringstream o;
    o << "\tModel: " << modelId << ", \"Model::" << quote(n.name()).substr(1, quote(n.name()).size() - 2) << "\", \""
      << (hasMesh ? "Mesh" : "Null") << "\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n"
      << "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\"," << num(t.x) << "," << num(t.y) << "," << num(t.z) << "\n"
      << "\t\t\tP: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\"," << num(a * deg) << "," << num(b * deg) << "," << num(c * deg) << "\n"
      << "\t\t\tP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\"," << num(s.x) << "," << num(s.y) << "," << num(s.z) << "\n"
      << "\t\t}\n\t\tShading: T\n\t\tCulling: \"CullingOff\"\n\t}\n";
    cx.objects += o.str();
    ++cx.modelCount;
    ++cx.rep->nodes;
    cx.conns += "\tC: \"OO\"," + std::to_string(modelId) + "," + std::to_string(parentId) + "\n";

    if (hasMesh) {
        const long long geomId = cx.nextId++;
        const size_t ntri = mesh.indices.size() / 3;
        // Material por triangulo.
        std::vector<int> triMat(ntri, n.materialIndex);
        for (const MaterialPart& p : n.materialParts)
            for (uint32_t k = p.firstIndex / 3; k < (p.firstIndex + p.indexCount) / 3 && k < ntri; ++k) triMat[k] = p.materialIndex;
        std::vector<int> local; // indices globales unicos
        std::vector<int> triLocal(ntri, 0);
        for (size_t k = 0; k < ntri; ++k) {
            auto it = std::find(local.begin(), local.end(), triMat[k]);
            if (it == local.end()) {
                local.push_back(triMat[k]);
                it = local.end() - 1;
            }
            triLocal[k] = (int)(it - local.begin());
        }
        std::ostringstream g;
        g << "\tGeometry: " << geomId << ", \"Geometry::\", \"Mesh\" {\n\t\tVertices: *" << mesh.vertices.size() * 3 << " {\n\t\t\ta: ";
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
            g << (i ? "," : "") << num(mesh.vertices[i].position.x) << "," << num(mesh.vertices[i].position.y) << "," << num(mesh.vertices[i].position.z);
        g << "\n\t\t}\n\t\tPolygonVertexIndex: *" << ntri * 3 << " {\n\t\t\ta: ";
        for (size_t k = 0; k < ntri; ++k)
            g << (k ? "," : "") << mesh.indices[k * 3] << "," << mesh.indices[k * 3 + 1] << "," << (-(long long)mesh.indices[k * 3 + 2] - 1);
        g << "\n\t\t}\n\t\tGeometryVersion: 124\n";
        g << "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
          << "\t\t\tReferenceInformationType: \"Direct\"\n\t\t\tNormals: *" << ntri * 9 << " {\n\t\t\t\ta: ";
        for (size_t i = 0; i < ntri * 3; ++i) {
            const glm::vec3& nn = mesh.vertices[mesh.indices[i]].normal;
            g << (i ? "," : "") << num(nn.x) << "," << num(nn.y) << "," << num(nn.z);
        }
        g << "\n\t\t\t}\n\t\t}\n";
        g << "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
          << "\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tUV: *" << mesh.vertices.size() * 2 << " {\n\t\t\t\ta: ";
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
            g << (i ? "," : "") << num(mesh.vertices[i].uv.x) << "," << num(1.0f - mesh.vertices[i].uv.y);
        g << "\n\t\t\t}\n\t\t\tUVIndex: *" << ntri * 3 << " {\n\t\t\t\ta: ";
        for (size_t i = 0; i < ntri * 3; ++i) g << (i ? "," : "") << mesh.indices[i];
        g << "\n\t\t\t}\n\t\t}\n";
        g << "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"ByPolygon\"\n"
          << "\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tMaterials: *" << ntri << " {\n\t\t\t\ta: ";
        for (size_t k = 0; k < ntri; ++k) g << (k ? "," : "") << triLocal[k];
        g << "\n\t\t\t}\n\t\t}\n";
        g << "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
          << "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
          << "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
          << "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n\t}\n";
        cx.objects += g.str();
        ++cx.geomCount;
        cx.conns += "\tC: \"OO\"," + std::to_string(geomId) + "," + std::to_string(modelId) + "\n";
        for (int gi : local) cx.conns += "\tC: \"OO\"," + std::to_string(materialId(cx, gi)) + "," + std::to_string(modelId) + "\n";
        cx.rep->points += (long long)mesh.vertices.size();
        cx.rep->polygons += (long long)ntri;
        ++cx.rep->meshes;
    }
    for (const auto& ch : n.children()) exportNode(cx, *ch, modelId);
}

} // namespace

bool exportFbx(const fs::path& path, const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
               FbxReport& report, std::string* error) {
    ExpCtx cx;
    cx.mats = &materials;
    cx.rep = &report;
    for (const SceneNode* r : roots) exportNode(cx, *r, 0);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "No se pudo escribir el archivo.";
        return false;
    }
    f << "; FBX 7.4.0 project file\n; Iron3D Studio\n\n"
      << "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n\tCreator: \"Iron3D Studio\"\n}\n"
      << "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
      << "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
      << "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
      << "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
      << "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n\t}\n}\n\n"
      << "Definitions:  {\n\tVersion: 100\n\tCount: " << (cx.modelCount + cx.geomCount + (int)cx.matIds.size()) << "\n"
      << "\tObjectType: \"Model\" {\n\t\tCount: " << cx.modelCount << "\n\t}\n"
      << "\tObjectType: \"Geometry\" {\n\t\tCount: " << cx.geomCount << "\n\t}\n"
      << "\tObjectType: \"Material\" {\n\t\tCount: " << cx.matIds.size() << "\n\t}\n}\n\n"
      << "Objects:  {\n" << cx.objects << "}\n\n"
      << "Connections:  {\n" << cx.conns << "}\n";
    return f.good();
}

} // namespace iron3d
