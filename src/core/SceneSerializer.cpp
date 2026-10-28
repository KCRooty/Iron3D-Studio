#include "SceneSerializer.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>

namespace iron3d {

namespace {

constexpr int kFormatVersion = 1;

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
        << " visible=" << (node.visible ? 1 : 0) << " {\n";
    for (const auto& child : node.children()) writeNode(out, *child, depth + 1);
    out << indent << "}\n";
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

bool parseNode(Parser& p, SceneNode& node, std::string& err) {
    if (p.atEnd() || p.next() != "NODE") { err = "se esperaba NODE"; return false; }

    std::string name = node.name();
    PrimitiveType prim = PrimitiveType::None;
    glm::vec3 pos(0.0f), rot(0.0f), scale(1.0f);
    bool visible = true;

    while (!p.atEnd() && p.peek() != "{") {
        const std::string tok = p.next();
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) continue; // token suelto/malformado: se ignora (tolerante)
        const std::string key = tok.substr(0, eq);
        std::string val = tok.substr(eq + 1);
        try {
            if (key == "name") {
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                    val = val.substr(1, val.size() - 2);
                name = val;
            } else if (key == "primitive") prim = primitiveFromCode(val);
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

    while (!p.atEnd() && p.peek() != "}") {
        if (p.peek() != "NODE") { err = "token inesperado: " + p.peek(); return false; }
        SceneNode* child = node.addChild(std::string());
        if (!parseNode(p, *child, err)) return false;
    }
    if (p.atEnd() || p.next() != "}") { err = "se esperaba '}' de cierre"; return false; }
    return true;
}

} // namespace

bool saveScene(const std::filesystem::path& path, const SceneNode& root, std::string* outError) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        if (outError) *outError = "no se pudo crear el archivo";
        return false;
    }
    file << "IRON3D_SCENE " << kFormatVersion << "\n";
    writeNode(file, root, 0);
    return true;
}

bool loadScene(const std::filesystem::path& path, SceneNode& root, std::string* outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (outError) *outError = "no se pudo abrir el archivo";
        return false;
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    const std::vector<std::string> tokens = tokenize(buf.str());

    if (tokens.size() < 2 || tokens[0] != "IRON3D_SCENE") {
        if (outError) *outError = "cabecera invalida (se esperaba 'IRON3D_SCENE <version>')";
        return false;
    }

    Parser p{tokens, 2}; // salta "IRON3D_SCENE" y el numero de version
    SceneNode temp("__scene_load_tmp__");
    std::string err;
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
    root.clearChildren();
    root.adoptChildrenFrom(temp);
    return true;
}

} // namespace iron3d
