#include "ObjIO.h"

#include "core/MeshData.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace iron3d {

namespace {

namespace fs = std::filesystem;

std::string lowerExt(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Resto de la linea tras la palabra clave (los nombres pueden llevar espacios).
std::string restOf(const std::string& line, size_t after) { return after < line.size() ? trim(line.substr(after)) : ""; }

struct Corner {
    int v = -1, vt = -1, vn = -1;
};

struct Face {
    std::vector<Corner> corners;
    int mat = -1; // indice local en la tabla de materiales del archivo
};

struct ObjectData {
    std::string name;
    std::vector<Face> faces;
};

int resolveIndex(int idx, size_t count) {
    if (idx > 0) return idx - 1;
    if (idx < 0) return (int)count + idx;
    return -1;
}

Corner parseCorner(const std::string& tok, size_t nv, size_t nt, size_t nn) {
    Corner c;
    int part = 0;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            const int raw = std::atoi(cur.c_str());
            if (part == 0) c.v = resolveIndex(raw, nv);
            else if (part == 1) c.vt = resolveIndex(raw, nt);
            else c.vn = resolveIndex(raw, nn);
        }
        cur.clear();
    };
    for (char ch : tok) {
        if (ch == '/') {
            flush();
            ++part;
        } else {
            cur.push_back(ch);
        }
    }
    flush();
    return c;
}

struct MtlEntry {
    Material mat;
};

void parseMtl(const fs::path& mtlPath, std::map<std::string, Material>& out, ObjReport& rep) {
    std::ifstream in(mtlPath);
    if (!in) {
        rep.warnings.push_back("No se pudo abrir el .mtl: " + mtlPath.filename().string());
        return;
    }
    Material* cur = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "newmtl") {
            const std::string name = restOf(line, 6);
            Material m;
            m.name = name.empty() ? "Material" : name;
            m.texBaseDir = mtlPath.parent_path().string();
            cur = &(out[name] = m);
        } else if (!cur) {
            continue;
        } else if (key == "Kd") {
            float r = 0.7f, g = 0.7f, b = 0.7f;
            ss >> r >> g >> b;
            cur->baseColor = glm::vec3(r, g, b);
        } else if (key == "Ns") {
            float ns = 0;
            ss >> ns;
            cur->roughness = std::clamp(1.0f - std::sqrt(std::clamp(ns, 0.0f, 1000.0f) / 1000.0f), 0.05f, 1.0f);
        } else if (key == "Ke") {
            float r = 0, g = 0, b = 0;
            ss >> r >> g >> b;
            cur->emission = glm::vec3(r, g, b);
            if (r + g + b > 0.0f) cur->useLuminance = true;
        } else if (key == "d") {
            float d = 1.0f;
            ss >> d;
            if (d < 1.0f) {
                cur->transparency = glm::vec3(1.0f - d);
                cur->useTransparency = true;
            }
        } else if (key == "map_Kd") {
            // map_Kd [opciones] archivo: se queda con el ultimo token (la ruta puede llevar espacios, se toma el resto).
            std::string rest = restOf(line, 6);
            const size_t sp = rest.find_last_of(' ');
            if (rest.rfind("-", 0) == 0 && sp != std::string::npos) rest = rest.substr(sp + 1);
            std::replace(rest.begin(), rest.end(), '\\', '/');
            cur->textures[(int)TexSlot::Color] = rest;
        }
    }
}

} // namespace

bool isObjPath(const fs::path& path) { return lowerExt(path) == ".obj"; }

bool importObj(const fs::path& path, SceneNode& root, std::vector<Material>& materials, ObjReport& report,
               std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "No se pudo abrir el archivo.";
        return false;
    }
    std::vector<glm::vec3> pos, nrm;
    std::vector<glm::vec2> uvs;
    std::vector<ObjectData> objects;
    std::vector<std::string> matNames; // por indice local
    std::map<std::string, Material> mtl;
    int curMat = -1;
    auto currentObject = [&]() -> ObjectData& {
        if (objects.empty()) objects.push_back({path.stem().string(), {}});
        return objects.back();
    };
    std::string line;
    int badFaces = 0;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "v") {
            float x = 0, y = 0, z = 0;
            ss >> x >> y >> z;
            pos.emplace_back(x, y, z);
        } else if (key == "vt") {
            float u = 0, v = 0;
            ss >> u >> v;
            uvs.emplace_back(u, 1.0f - v); // v hacia abajo, como C4D
        } else if (key == "vn") {
            float x = 0, y = 0, z = 0;
            ss >> x >> y >> z;
            nrm.emplace_back(x, y, z);
        } else if (key == "o" || key == "g") {
            const std::string name = restOf(line, key.size());
            // Un `g` justo tras un `o` sin caras no abre otro objeto.
            if (!objects.empty() && objects.back().faces.empty()) objects.back().name = name.empty() ? objects.back().name : name;
            else objects.push_back({name.empty() ? "Objeto" : name, {}});
        } else if (key == "mtllib") {
            const std::string file = restOf(line, 6);
            parseMtl(path.parent_path() / file, mtl, report);
        } else if (key == "usemtl") {
            const std::string name = restOf(line, 6);
            auto it = std::find(matNames.begin(), matNames.end(), name);
            if (it == matNames.end()) {
                matNames.push_back(name);
                it = matNames.end() - 1;
            }
            curMat = (int)(it - matNames.begin());
        } else if (key == "f") {
            Face f;
            f.mat = curMat;
            std::string tok;
            while (ss >> tok) {
                const Corner c = parseCorner(tok, pos.size(), uvs.size(), nrm.size());
                if (c.v < 0 || (size_t)c.v >= pos.size()) {
                    f.corners.clear();
                    break;
                }
                f.corners.push_back(c);
            }
            if (f.corners.size() >= 3) currentObject().faces.push_back(std::move(f));
            else ++badFaces;
        }
    }
    if (pos.empty()) {
        if (error) *error = "El archivo no tiene vértices (¿es un OBJ?).";
        return false;
    }
    if (badFaces) report.warnings.push_back("Caras sin vértices válidos omitidas (" + std::to_string(badFaces) + ")");

    // Materiales usados -> indices globales.
    const int baseMat = (int)materials.size();
    std::vector<int> globalMat(matNames.size(), -1);
    for (size_t i = 0; i < matNames.size(); ++i) {
        Material m;
        auto it = mtl.find(matNames[i]);
        if (it != mtl.end()) m = it->second;
        else {
            m.name = matNames[i].empty() ? "Material" : matNames[i];
            if (!matNames[i].empty()) report.warnings.push_back("Material sin definir en el .mtl: " + matNames[i]);
        }
        globalMat[i] = (int)materials.size();
        materials.push_back(std::move(m));
        ++report.materials;
    }
    (void)baseMat;

    for (ObjectData& obj : objects) {
        if (obj.faces.empty()) continue;
        // Caras agrupadas por material (estable) para poder dar un rango por material.
        std::stable_sort(obj.faces.begin(), obj.faces.end(), [](const Face& a, const Face& b) { return a.mat < b.mat; });

        auto mesh = std::make_shared<MeshData>();
        std::map<std::tuple<int, int, int>, uint32_t> unique;
        std::vector<std::vector<uint32_t>> faceVerts;
        for (const Face& f : obj.faces) {
            std::vector<uint32_t> ids;
            for (const Corner& c : f.corners) {
                const auto key = std::make_tuple(c.v, c.vt, c.vn);
                auto it = unique.find(key);
                if (it == unique.end()) {
                    Vertex v{};
                    v.position = pos[(size_t)c.v];
                    v.color = glm::vec3(1.0f);
                    if (c.vt >= 0 && (size_t)c.vt < uvs.size()) v.uv = uvs[(size_t)c.vt];
                    if (c.vn >= 0 && (size_t)c.vn < nrm.size()) {
                        v.normal = glm::normalize(nrm[(size_t)c.vn]);
                        if (!std::isfinite(v.normal.x)) v.normal = glm::vec3(0, 0, 1);
                    } else {
                        v.normal = glm::vec3(0.0f); // se calcula abajo
                    }
                    it = unique.emplace(key, (uint32_t)mesh->vertices.size()).first;
                    mesh->vertices.push_back(v);
                }
                ids.push_back(it->second);
            }
            faceVerts.push_back(std::move(ids));
        }
        // Triangulacion en abanico; faceTris guarda cuantos triangulos forma cada cara.
        for (const auto& ids : faceVerts) {
            for (size_t k = 1; k + 1 < ids.size(); ++k) {
                mesh->indices.push_back(ids[0]);
                mesh->indices.push_back(ids[k]);
                mesh->indices.push_back(ids[k + 1]);
            }
            mesh->faceTris.push_back((uint32_t)(ids.size() - 2));
        }
        // Normales suaves (por posicion) para los vertices que no traian una.
        {
            std::map<std::tuple<float, float, float>, glm::vec3> acc;
            auto key = [](const glm::vec3& p) { return std::make_tuple(p.x, p.y, p.z); };
            for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
                const glm::vec3& a = mesh->vertices[mesh->indices[i]].position;
                const glm::vec3& b = mesh->vertices[mesh->indices[i + 1]].position;
                const glm::vec3& c = mesh->vertices[mesh->indices[i + 2]].position;
                const glm::vec3 n = glm::cross(b - a, c - a); // ponderada por el area
                for (int k = 0; k < 3; ++k) acc[key(mesh->vertices[mesh->indices[i + (size_t)k]].position)] += n;
            }
            for (Vertex& v : mesh->vertices) {
                if (glm::dot(v.normal, v.normal) > 0.0f) continue;
                const glm::vec3 n = acc[key(v.position)];
                v.normal = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : glm::vec3(0, 0, 1);
            }
        }
        updateBounds(*mesh);

        SceneNode* node = root.addChild(obj.name, PrimitiveType::Mesh);
        // Partes por material.
        std::vector<MaterialPart> parts;
        uint32_t triCursor = 0;
        for (size_t fi = 0; fi < obj.faces.size(); ++fi) {
            const uint32_t tris = mesh->faceTris[fi];
            const int mat = obj.faces[fi].mat;
            const int gm = mat >= 0 ? globalMat[(size_t)mat] : -1;
            if (parts.empty() || parts.back().materialIndex != gm) {
                MaterialPart p;
                p.firstIndex = triCursor * 3;
                p.indexCount = 0;
                p.materialIndex = gm;
                parts.push_back(p);
            }
            parts.back().indexCount += tris * 3;
            triCursor += tris;
        }
        if (parts.size() == 1) {
            node->materialIndex = parts[0].materialIndex;
        } else if (parts.size() > 1) {
            node->materialIndex = parts[0].materialIndex;
            node->materialParts = std::move(parts);
        }
        node->mesh = std::move(mesh);
        ++report.objects;
        report.points += (long long)node->mesh->vertices.size();
        report.polygons += (long long)obj.faces.size();
    }
    if (report.objects == 0) {
        if (error) *error = "El archivo no tiene caras.";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Exportar
// ---------------------------------------------------------------------------

namespace {

struct ExportCtx {
    std::ofstream obj;
    std::vector<Material> usedMats;       // materiales escritos al .mtl
    std::map<int, std::string> matName;   // indice de escena -> nombre unico en el .mtl
    const std::vector<Material>* mats = nullptr;
    size_t vBase = 1, vtBase = 1, vnBase = 1;
    ObjReport* rep = nullptr;
};

std::string safeName(std::string s) {
    for (char& c : s)
        if (std::isspace((unsigned char)c)) c = '_';
    return s.empty() ? "Objeto" : s;
}

const std::string& mtlNameFor(ExportCtx& cx, int matIndex) {
    auto it = cx.matName.find(matIndex);
    if (it != cx.matName.end()) return it->second;
    const Material& m = (*cx.mats)[(size_t)matIndex];
    std::string name = safeName(m.name);
    int n = 1;
    std::string unique = name;
    auto taken = [&](const std::string& s) {
        for (const auto& kv : cx.matName)
            if (kv.second == s) return true;
        return false;
    };
    while (taken(unique)) unique = name + "_" + std::to_string(++n);
    cx.usedMats.push_back(m);
    return cx.matName.emplace(matIndex, unique).first->second;
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

void writeNode(ExportCtx& cx, const SceneNode& n) {
    MeshData mesh = meshOf(n);
    if (!mesh.vertices.empty() && !mesh.indices.empty()) {
        const glm::mat4 w = n.worldMatrix();
        const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(w)));
        cx.obj << "o " << safeName(n.name()) << "\n";
        for (const Vertex& v : mesh.vertices) {
            const glm::vec3 p = glm::vec3(w * glm::vec4(v.position, 1.0f));
            cx.obj << "v " << p.x << " " << p.y << " " << p.z << "\n";
        }
        for (const Vertex& v : mesh.vertices) cx.obj << "vt " << v.uv.x << " " << (1.0f - v.uv.y) << "\n";
        for (const Vertex& v : mesh.vertices) {
            glm::vec3 nn = nm * v.normal;
            nn = glm::dot(nn, nn) > 0 ? glm::normalize(nn) : glm::vec3(0, 0, 1);
            cx.obj << "vn " << nn.x << " " << nn.y << " " << nn.z << "\n";
        }
        // Caras: con faceTris se reconstruyen los poligonos del abanico.
        auto idxStr = [&](uint32_t i) {
            const size_t k = (size_t)i;
            std::ostringstream s;
            s << (cx.vBase + k) << "/" << (cx.vtBase + k) << "/" << (cx.vnBase + k);
            return s.str();
        };
        const size_t triCount = mesh.indices.size() / 3;
        // Material de cada triangulo: partes primero, materialIndex por defecto.
        auto matOfTri = [&](size_t tri) {
            const uint32_t first = (uint32_t)(tri * 3);
            for (const MaterialPart& p : n.materialParts)
                if (first >= p.firstIndex && first < p.firstIndex + p.indexCount) return p.materialIndex;
            return n.materialIndex;
        };
        int lastMat = -2;
        size_t tri = 0, faceIdx = 0;
        while (tri < triCount) {
            const size_t k = mesh.faceTris.empty() || faceIdx >= mesh.faceTris.size() ? 1 : (size_t)mesh.faceTris[faceIdx];
            const int mat = matOfTri(tri);
            if (mat != lastMat) {
                if (mat >= 0 && (size_t)mat < cx.mats->size()) cx.obj << "usemtl " << mtlNameFor(cx, mat) << "\n";
                else cx.obj << "usemtl None\n";
                lastMat = mat;
            }
            std::vector<uint32_t> poly = {mesh.indices[tri * 3], mesh.indices[tri * 3 + 1], mesh.indices[tri * 3 + 2]};
            for (size_t j = 1; j < k && tri + j < triCount; ++j) poly.push_back(mesh.indices[(tri + j) * 3 + 2]);
            cx.obj << "f";
            for (uint32_t i : poly) cx.obj << " " << idxStr(i);
            cx.obj << "\n";
            ++cx.rep->polygons;
            tri += k;
            ++faceIdx;
        }
        cx.vBase += mesh.vertices.size();
        cx.vtBase += mesh.vertices.size();
        cx.vnBase += mesh.vertices.size();
        ++cx.rep->objects;
        cx.rep->points += (long long)mesh.vertices.size();
    }
    for (const auto& c : n.children()) writeNode(cx, *c);
}

} // namespace

bool exportObj(const fs::path& path, const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
               ObjReport& report, std::string* error) {
    ExportCtx cx;
    cx.obj.open(path);
    if (!cx.obj) {
        if (error) *error = "No se pudo crear el archivo.";
        return false;
    }
    cx.mats = &materials;
    cx.rep = &report;
    const fs::path mtlPath = fs::path(path).replace_extension(".mtl");
    cx.obj.precision(7);
    cx.obj << "# Exportado desde Iron3D Studio\nmtllib " << mtlPath.filename().string() << "\n";
    for (const SceneNode* r : roots) writeNode(cx, *r);
    cx.obj.close();
    if (report.objects == 0) {
        if (error) *error = "No hay objetos con malla que exportar.";
        return false;
    }
    std::ofstream mtl(mtlPath);
    if (!mtl) {
        report.warnings.push_back("No se pudo escribir el .mtl.");
        return true;
    }
    mtl << "# Materiales de Iron3D Studio\nnewmtl None\nKd 0.7 0.7 0.7\n\n";
    for (const auto& kv : cx.matName) {
        const Material& m = materials[(size_t)kv.first];
        mtl << "newmtl " << kv.second << "\n";
        mtl << "Kd " << m.baseColor.x << " " << m.baseColor.y << " " << m.baseColor.z << "\n";
        mtl << "Ns " << (1.0f - m.roughness) * (1.0f - m.roughness) * 1000.0f << "\n";
        if (m.useLuminance) mtl << "Ke " << m.emission.x << " " << m.emission.y << " " << m.emission.z << "\n";
        if (m.useTransparency) mtl << "d " << std::clamp(1.0f - m.transparency.x, 0.0f, 1.0f) << "\n";
        const std::string& tex = m.textures[(int)TexSlot::Color];
        if (!tex.empty()) mtl << "map_Kd " << tex << "\n";
        mtl << "\n";
        ++report.materials;
    }
    return true;
}

} // namespace iron3d
