#include "SceneSerializer.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>

namespace iron3d {

namespace {

constexpr int kFormatVersion = 3; // v2 = Fase 5: bloque MATERIALS + materialIndex; v3 = mallas (MESH)

const char* primitiveCode(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "cube";
        case PrimitiveType::Sphere: return "sphere";
        case PrimitiveType::Plane: return "plane";
        case PrimitiveType::Light: return "light";
        case PrimitiveType::Mesh: return "mesh";
        case PrimitiveType::None: default: return "none";
    }
}

PrimitiveType primitiveFromCode(const std::string& code) {
    if (code == "cube") return PrimitiveType::Cube;
    if (code == "sphere") return PrimitiveType::Sphere;
    if (code == "plane") return PrimitiveType::Plane;
    if (code == "light") return PrimitiveType::Light;
    if (code == "mesh") return PrimitiveType::Mesh;
    return PrimitiveType::None;
}

// Bloque MESH: nv vertices (posicion + normal, el color no se guarda) y ni
// indices, todo en una linea por vertice / por triangulo.
void writeMesh(std::ostream& out, const MeshData& mesh, const std::string& indent) {
    out << indent << "  MESH nv=" << mesh.vertices.size() << " ni=" << mesh.indices.size() << " {\n";
    for (const Vertex& v : mesh.vertices)
        out << indent << "    " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << ' ' << v.normal.x
            << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        out << indent << "    " << mesh.indices[i] << ' ' << mesh.indices[i + 1] << ' ' << mesh.indices[i + 2] << '\n';
    out << indent << "  }\n";
}

void writeNode(std::ostream& out, const SceneNode& node, int depth, MeshRefTable* meshRefs) {
    const std::string indent((size_t)depth * 2, ' ');
    out << indent << "NODE name=\"" << node.name() << "\" primitive=" << primitiveCode(node.primitive)
        << " px=" << node.position.x << " py=" << node.position.y << " pz=" << node.position.z
        << " rx=" << node.rotationDeg.x << " ry=" << node.rotationDeg.y << " rz=" << node.rotationDeg.z
        << " sx=" << node.scale.x << " sy=" << node.scale.y << " sz=" << node.scale.z
        << " editorVis=" << (int)node.editorVis << " renderVis=" << (int)node.renderVis
        << " materialIndex=" << node.materialIndex;
    const bool hasMesh = node.primitive == PrimitiveType::Mesh && node.mesh;
    if (hasMesh && meshRefs) out << " meshref=" << meshRefs->ref(node.mesh);
    out << " {\n";
    if (hasMesh && !meshRefs) writeMesh(out, *node.mesh, indent);
    for (const auto& child : node.children()) writeNode(out, *child, depth + 1, meshRefs);
    out << indent << "}\n";
}

void writeMaterials(std::ostream& out, const std::vector<Material>& materials) {
    out << "MATERIALS {\n";
    for (const Material& m : materials) {
        out << "  MATERIAL name=\"" << m.name << "\" br=" << m.baseColor.r << " bg=" << m.baseColor.g
            << " bb=" << m.baseColor.b << " metallic=" << m.metallic << " roughness=" << m.roughness << " {\n  }\n";
    }
    out << "}\n";
}

// Tokenizador consciente de comillas: una comilla abre/cierra un tramo donde
// los espacios NO separan token (asi `name="Nuevo objeto"` es UN solo token
// pese al espacio interno). `{`/`}` siempre son su propio token.
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuotes = false;
    auto flush = [&]() { if (!cur.empty()) { tokens.push_back(cur); cur.clear(); } };
    for (char c : text) {
        if (c == '"') { inQuotes = !inQuotes; cur += c; continue; }
        if (!inQuotes && std::isspace((unsigned char)c)) { flush(); continue; }
        if (!inQuotes && (c == '{' || c == '}')) { flush(); tokens.push_back(std::string(1, c)); continue; }
        cur += c;
    }
    flush();
    return tokens;
}

struct Parser {
    const std::vector<std::string>& toks;
    size_t pos = 0;
    bool atEnd() const { return pos >= toks.size(); }
    const std::string& peek() const { return toks[pos]; }
    std::string next() { return toks[pos++]; }
};

std::string unquote(std::string val) {
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') return val.substr(1, val.size() - 2);
    return val;
}

bool parseMesh(Parser& p, std::shared_ptr<const MeshData>& out, std::string& err) {
    p.next(); // "MESH"
    size_t nv = 0, ni = 0;
    while (!p.atEnd() && p.peek() != "{") {
        const std::string tok = p.next();
        try {
            if (tok.rfind("nv=", 0) == 0) nv = (size_t)std::stoul(tok.substr(3));
            else if (tok.rfind("ni=", 0) == 0) ni = (size_t)std::stoul(tok.substr(3));
        } catch (const std::exception&) {
            err = "cabecera MESH invalida";
            return false;
        }
    }
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras MESH"; return false; }
    if (p.toks.size() - p.pos < nv * 6 + ni) { err = "bloque MESH truncado"; return false; }
    auto mesh = std::make_shared<MeshData>();
    mesh->vertices.resize(nv);
    mesh->indices.resize(ni);
    try {
        for (size_t i = 0; i < nv; ++i) {
            Vertex& v = mesh->vertices[i];
            v.position.x = std::stof(p.next()); v.position.y = std::stof(p.next()); v.position.z = std::stof(p.next());
            v.normal.x = std::stof(p.next()); v.normal.y = std::stof(p.next()); v.normal.z = std::stof(p.next());
            v.color = glm::vec3(0.8f);
        }
        for (size_t i = 0; i < ni; ++i) {
            const unsigned long idx = std::stoul(p.next());
            if (idx >= nv) { err = "indice de MESH fuera de rango"; return false; }
            mesh->indices[i] = (uint32_t)idx;
        }
    } catch (const std::exception&) {
        err = "valor numerico invalido en MESH";
        return false;
    }
    if (p.atEnd() || p.next() != "}") { err = "se esperaba '}' de cierre de MESH"; return false; }
    updateBounds(*mesh);
    out = std::move(mesh);
    return true;
}

bool parseNode(Parser& p, SceneNode& node, std::string& err, const MeshRefTable* meshRefs) {
    if (p.atEnd() || p.next() != "NODE") { err = "se esperaba NODE"; return false; }

    std::string name = node.name();
    PrimitiveType prim = PrimitiveType::None;
    glm::vec3 pos(0.0f), rot(0.0f), scale(1.0f);
    VisMode editorVis = VisMode::Default, renderVis = VisMode::Default;
    int materialIndex = -1;
    std::shared_ptr<const MeshData> mesh;

    while (!p.atEnd() && p.peek() != "{") {
        const std::string tok = p.next();
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) continue; // token suelto/malformado: se ignora (tolerante)
        const std::string key = tok.substr(0, eq);
        std::string val = tok.substr(eq + 1);
        try {
            if (key == "name") name = unquote(val);
            else if (key == "primitive") prim = primitiveFromCode(val);
            else if (key == "px") pos.x = std::stof(val);
            else if (key == "py") pos.y = std::stof(val);
            else if (key == "pz") pos.z = std::stof(val);
            else if (key == "rx") rot.x = std::stof(val);
            else if (key == "ry") rot.y = std::stof(val);
            else if (key == "rz") rot.z = std::stof(val);
            else if (key == "sx") scale.x = std::stof(val);
            else if (key == "sy") scale.y = std::stof(val);
            else if (key == "sz") scale.z = std::stof(val);
            // .iron antiguos: un solo `visible`; 0 = oculto en visor y render.
            else if (key == "visible") { if (val == "0") editorVis = renderVis = VisMode::Off; }
            else if (key == "editorVis") editorVis = (VisMode)std::clamp(std::stoi(val), 0, 2);
            else if (key == "renderVis") renderVis = (VisMode)std::clamp(std::stoi(val), 0, 2);
            else if (key == "materialIndex") materialIndex = std::stoi(val);
            else if (key == "meshref") {
                const int r = std::stoi(val);
                if (!meshRefs || r < 0 || (size_t)r >= meshRefs->meshes.size()) {
                    err = "meshref sin tabla de mallas";
                    return false;
                }
                mesh = meshRefs->meshes[(size_t)r];
            }
        } catch (const std::exception&) {
            err = "valor numerico invalido en '" + tok + "'";
            return false;
        }
    }
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras los atributos de NODE"; return false; }
    if (!p.atEnd() && p.peek() == "MESH" && !parseMesh(p, mesh, err)) return false;

    node.setName(name);
    node.primitive = prim;
    node.position = pos;
    node.rotationDeg = rot;
    node.scale = scale;
    node.editorVis = editorVis;
    node.renderVis = renderVis;
    node.materialIndex = materialIndex;
    node.mesh = std::move(mesh);
    if (node.primitive == PrimitiveType::Mesh && !node.mesh) node.primitive = PrimitiveType::None;

    while (!p.atEnd() && p.peek() != "}") {
        if (p.peek() != "NODE") { err = "token inesperado: " + p.peek(); return false; }
        SceneNode* child = node.addChild(std::string());
        if (!parseNode(p, *child, err, meshRefs)) return false;
    }
    if (p.atEnd() || p.next() != "}") { err = "se esperaba '}' de cierre"; return false; }
    return true;
}

bool parseMaterial(Parser& p, Material& out, std::string& err) {
    if (p.atEnd() || p.next() != "MATERIAL") { err = "se esperaba MATERIAL"; return false; }
    while (!p.atEnd() && p.peek() != "{") {
        const std::string tok = p.next();
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = tok.substr(0, eq);
        std::string val = tok.substr(eq + 1);
        try {
            if (key == "name") out.name = unquote(val);
            else if (key == "br") out.baseColor.r = std::stof(val);
            else if (key == "bg") out.baseColor.g = std::stof(val);
            else if (key == "bb") out.baseColor.b = std::stof(val);
            else if (key == "metallic") out.metallic = std::stof(val);
            else if (key == "roughness") out.roughness = std::stof(val);
        } catch (const std::exception&) {
            err = "valor numerico invalido en '" + tok + "'";
            return false;
        }
    }
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras los atributos de MATERIAL"; return false; }
    if (p.atEnd() || p.next() != "}") { err = "se esperaba '}' de cierre de MATERIAL"; return false; }
    return true;
}

// Bloque MATERIALS opcional: solo se consume si el siguiente token es
// literalmente "MATERIALS" -- un .iron de antes de la Fase 5 no lo tiene y
// el parseo sigue igual, directo al NODE raiz.
bool parseMaterialsBlockIfPresent(Parser& p, std::vector<Material>& materials, std::string& err) {
    if (p.atEnd() || p.peek() != "MATERIALS") return true; // nada que hacer, no es un error
    p.next(); // "MATERIALS"
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras MATERIALS"; return false; }
    while (!p.atEnd() && p.peek() != "}") {
        Material m;
        if (!parseMaterial(p, m, err)) return false;
        materials.push_back(std::move(m));
    }
    if (p.atEnd() || p.next() != "}") { err = "se esperaba '}' de cierre de MATERIALS"; return false; }
    return true;
}

} // namespace

int MeshRefTable::ref(const std::shared_ptr<const MeshData>& mesh) {
    auto it = index.find(mesh.get());
    if (it != index.end()) return it->second;
    const int r = (int)meshes.size();
    meshes.push_back(mesh);
    index.emplace(mesh.get(), r);
    return r;
}

std::string serializeScene(const SceneNode& root, const std::vector<Material>& materials, MeshRefTable* meshRefs) {
    std::ostringstream out;
    out << "IRON3D_SCENE " << kFormatVersion << "\n";
    writeMaterials(out, materials);
    writeNode(out, root, 0, meshRefs);
    return out.str();
}

bool deserializeScene(const std::string& text, SceneNode& root, std::vector<Material>& materials,
                       std::string* outError, const MeshRefTable* meshRefs) {
    const std::vector<std::string> tokens = tokenize(text);

    if (tokens.size() < 2 || tokens[0] != "IRON3D_SCENE") {
        if (outError) *outError = "cabecera invalida (se esperaba 'IRON3D_SCENE <version>')";
        return false;
    }

    Parser p{tokens, 2}; // salta "IRON3D_SCENE" y el numero de version
    std::vector<Material> tempMaterials;
    std::string err;
    if (!parseMaterialsBlockIfPresent(p, tempMaterials, err)) {
        if (outError) *outError = err;
        return false;
    }

    SceneNode temp("__scene_load_tmp__");
    if (!parseNode(p, temp, err, meshRefs)) {
        if (outError) *outError = err;
        return false;
    }

    root.setName(temp.name());
    root.primitive = temp.primitive;
    root.position = temp.position;
    root.rotationDeg = temp.rotationDeg;
    root.scale = temp.scale;
    root.editorVis = temp.editorVis;
    root.renderVis = temp.renderVis;
    root.materialIndex = temp.materialIndex;
    root.mesh = temp.mesh;
    root.clearChildren();
    root.adoptChildrenFrom(temp);
    materials = std::move(tempMaterials);
    return true;
}

bool saveScene(const std::filesystem::path& path, const SceneNode& root, const std::vector<Material>& materials,
               std::string* outError) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        if (outError) *outError = "no se pudo crear el archivo";
        return false;
    }
    file << serializeScene(root, materials);
    return true;
}

bool loadScene(const std::filesystem::path& path, SceneNode& root, std::vector<Material>& materials,
               std::string* outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (outError) *outError = "no se pudo abrir el archivo";
        return false;
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    return deserializeScene(buf.str(), root, materials, outError);
}

} // namespace iron3d
