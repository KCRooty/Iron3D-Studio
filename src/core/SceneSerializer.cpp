#include "SceneSerializer.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>

namespace iron3d {

namespace {

constexpr int kFormatVersion = 2; // v2 = Fase 5: bloque MATERIALS + materialIndex por nodo

const char* primitiveCode(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "cube";
        case PrimitiveType::Sphere: return "sphere";
        case PrimitiveType::Plane: return "plane";
        case PrimitiveType::Light: return "light";
        case PrimitiveType::None: default: return "none";
    }
}

PrimitiveType primitiveFromCode(const std::string& code) {
    if (code == "cube") return PrimitiveType::Cube;
    if (code == "sphere") return PrimitiveType::Sphere;
    if (code == "plane") return PrimitiveType::Plane;
    if (code == "light") return PrimitiveType::Light;
    return PrimitiveType::None;
}

void writeNode(std::ostream& out, const SceneNode& node, int depth) {
    const std::string indent((size_t)depth * 2, ' ');
    out << indent << "NODE name=\"" << node.name() << "\" primitive=" << primitiveCode(node.primitive)
        << " px=" << node.position.x << " py=" << node.position.y << " pz=" << node.position.z
        << " rx=" << node.rotationDeg.x << " ry=" << node.rotationDeg.y << " rz=" << node.rotationDeg.z
        << " sx=" << node.scale.x << " sy=" << node.scale.y << " sz=" << node.scale.z
        << " visible=" << (node.visible ? 1 : 0) << " materialIndex=" << node.materialIndex << " {\n";
    for (const auto& child : node.children()) writeNode(out, *child, depth + 1);
    out << indent << "}\n";
}

void writeMaterials(std::ostream& out, const std::vector<Material>& materials) {
    out << "MATERIALS {\n";
    for (const Material& m : materials) {
        out << "  MATERIAL name=\"" << m.name << "\" dr=" << m.diffuse.r << " dg=" << m.diffuse.g
            << " db=" << m.diffuse.b << " specular=" << m.specular << " shininess=" << m.shininess << " {\n  }\n";
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

bool parseNode(Parser& p, SceneNode& node, std::string& err) {
    if (p.atEnd() || p.next() != "NODE") { err = "se esperaba NODE"; return false; }

    std::string name = node.name();
    PrimitiveType prim = PrimitiveType::None;
    glm::vec3 pos(0.0f), rot(0.0f), scale(1.0f);
    bool visible = true;
    int materialIndex = -1;

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
            else if (key == "visible") visible = (val != "0");
            else if (key == "materialIndex") materialIndex = std::stoi(val);
        } catch (const std::exception&) {
            err = "valor numerico invalido en '" + tok + "'";
            return false;
        }
    }
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras los atributos de NODE"; return false; }

    node.setName(name);
    node.primitive = prim;
    node.position = pos;
    node.rotationDeg = rot;
    node.scale = scale;
    node.visible = visible;
    node.materialIndex = materialIndex;

    while (!p.atEnd() && p.peek() != "}") {
        if (p.peek() != "NODE") { err = "token inesperado: " + p.peek(); return false; }
        SceneNode* child = node.addChild(std::string());
        if (!parseNode(p, *child, err)) return false;
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
            else if (key == "dr") out.diffuse.r = std::stof(val);
            else if (key == "dg") out.diffuse.g = std::stof(val);
            else if (key == "db") out.diffuse.b = std::stof(val);
            else if (key == "specular") out.specular = std::stof(val);
            else if (key == "shininess") out.shininess = std::stof(val);
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

std::string serializeScene(const SceneNode& root, const std::vector<Material>& materials) {
    std::ostringstream out;
    out << "IRON3D_SCENE " << kFormatVersion << "\n";
    writeMaterials(out, materials);
    writeNode(out, root, 0);
    return out.str();
}

bool deserializeScene(const std::string& text, SceneNode& root, std::vector<Material>& materials,
                       std::string* outError) {
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
    if (!parseNode(p, temp, err)) {
        if (outError) *outError = err;
        return false;
    }

    root.setName(temp.name());
    root.primitive = temp.primitive;
    root.position = temp.position;
    root.rotationDeg = temp.rotationDeg;
    root.scale = temp.scale;
    root.visible = temp.visible;
    root.materialIndex = temp.materialIndex;
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
