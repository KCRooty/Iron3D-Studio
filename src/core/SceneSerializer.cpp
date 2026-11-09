#include "SceneSerializer.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>
#include <cctype>

namespace iron3d {

namespace {

// v2 = Fase 5: bloque MATERIALS + materialIndex; v3 = mallas (MESH);
// v4 (2026-11-06) = rotaciones en HPB de C4D. Al cargar v1-v3 se convierten
// (misma orientacion, otros numeros), ver convertLegacyRotations.
// 5: pistas de animacion (KEY); 6: poligonos (FACES) e interpolacion de claves;
// 7 (2026-11-07): floats con 9 cifras (ida y vuelta exacta), nombres
// escapados, UV en MESH, LIGHT / CAMERA / TEXTAG en los nodos y canales,
// texturas y capas de Reflectancia en los materiales.
constexpr int kFormatVersion = 7;

// Texto entre comillas: '%', '"' y saltos de linea van como %25, %22, %0A.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '%') out += "%25";
        else if (c == '"') out += "%22";
        else if (c == '\n') out += "%0A";
        else if (c == '\r') out += "%0D";
        else out += c;
    }
    return out + "\"";
}
std::string unescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const std::string hex = text.substr(i + 1, 2);
            if (hex == "25") { out += '%'; i += 2; continue; }
            if (hex == "22") { out += '"'; i += 2; continue; }
            if (hex == "0A") { out += '\n'; i += 2; continue; }
            if (hex == "0D") { out += '\r'; i += 2; continue; }
        }
        out += text[i];
    }
    return out;
}
int g_readVersion = kFormatVersion; // version del archivo que se esta leyendo

void convertLegacyRotations(SceneNode& n) {
    n.rotationDeg = mat3ToHpb(legacyXyzToMat3(n.rotationDeg));
    for (const auto& c : n.children()) convertLegacyRotations(*c);
}

const char* primitiveCode(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "cube";
        case PrimitiveType::Sphere: return "sphere";
        case PrimitiveType::Plane: return "plane";
        case PrimitiveType::Light: return "light";
        case PrimitiveType::Mesh: return "mesh";
        case PrimitiveType::Camera: return "camera";
        case PrimitiveType::None: default: return "none";
    }
}

PrimitiveType primitiveFromCode(const std::string& code) {
    if (code == "cube") return PrimitiveType::Cube;
    if (code == "sphere") return PrimitiveType::Sphere;
    if (code == "plane") return PrimitiveType::Plane;
    if (code == "light") return PrimitiveType::Light;
    if (code == "mesh") return PrimitiveType::Mesh;
    if (code == "camera") return PrimitiveType::Camera;
    return PrimitiveType::None;
}

// Bloque MESH: nv vertices (posicion + normal, el color no se guarda) y ni
// indices, todo en una linea por vertice / por triangulo.
void writeMesh(std::ostream& out, const MeshData& mesh, const std::string& indent) {
    // Formato 7: uv=1 = dos numeros mas por vertice (solo si alguna UV no es 0).
    const bool uv = std::any_of(mesh.vertices.begin(), mesh.vertices.end(),
                                [](const Vertex& v) { return v.uv.x != 0.0f || v.uv.y != 0.0f; });
    out << indent << "  MESH nv=" << mesh.vertices.size() << " ni=" << mesh.indices.size() << (uv ? " uv=1" : "")
        << " {\n";
    for (const Vertex& v : mesh.vertices) {
        out << indent << "    " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << ' ' << v.normal.x
            << ' ' << v.normal.y << ' ' << v.normal.z;
        if (uv) out << ' ' << v.uv.x << ' ' << v.uv.y;
        out << '\n';
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        out << indent << "    " << mesh.indices[i] << ' ' << mesh.indices[i + 1] << ' ' << mesh.indices[i + 2] << '\n';
    out << indent << "  }\n";
    // Formato 6: triangulos por poligono (solo si la malla los trae).
    if (!mesh.faceTris.empty()) {
        out << indent << "  FACES n=" << mesh.faceTris.size() << " {";
        for (size_t i = 0; i < mesh.faceTris.size(); ++i) out << (i % 32 == 0 ? "\n" + indent + "    " : " ") << mesh.faceTris[i];
        out << '\n' << indent << "  }\n";
    }
}

void writeNode(std::ostream& out, const SceneNode& node, int depth, MeshRefTable* meshRefs) {
    const std::string indent((size_t)depth * 2, ' ');
    out << indent << "NODE name=" << quoted(node.name()) << " primitive=" << primitiveCode(node.primitive)
        << " px=" << node.position.x << " py=" << node.position.y << " pz=" << node.position.z
        << " rx=" << node.rotationDeg.x << " ry=" << node.rotationDeg.y << " rz=" << node.rotationDeg.z
        << " sx=" << node.scale.x << " sy=" << node.scale.y << " sz=" << node.scale.z
        << " editorVis=" << (int)node.editorVis << " renderVis=" << (int)node.renderVis
        << " materialIndex=" << node.materialIndex;
    const bool hasMesh = node.primitive == PrimitiveType::Mesh && node.mesh;
    if (hasMesh && meshRefs) out << " meshref=" << meshRefs->ref(node.mesh);
    out << " {\n";
    if (hasMesh && !meshRefs) writeMesh(out, *node.mesh, indent);
    if (node.primitive == PrimitiveType::Light) {
        const LightData& l = node.light;
        out << indent << "  LIGHT kind=" << (int)l.kind << " r=" << l.color.r << " g=" << l.color.g << " b=" << l.color.b
            << " i=" << l.intensity << " outer=" << l.outerAngleDeg << " inner=" << l.innerAngleDeg
            << " shadow=" << l.shadow << " falloff=" << l.falloff << " radius=" << l.falloffRadius
            << " iradius=" << l.innerRadius << " shape=" << l.areaShape << " ax=" << l.areaSize.x
            << " ay=" << l.areaSize.y << " az=" << l.areaSize.z << "\n";
    }
    if (node.primitive == PrimitiveType::Camera) {
        const CameraData& c = node.camera;
        out << indent << "  CAMERA proj=" << c.projection << " focal=" << c.focalMm << " sensor=" << c.sensorMm
            << " zoom=" << c.zoom << " target=" << quoted(c.target) << "\n";
    }
    if (node.materialIndex >= 0 && !(node.texTag == TextureTagData{})) {
        const TextureTagData& t = node.texTag;
        out << indent << "  TEXTAG proj=" << t.projection << " ou=" << t.offset.x << " ov=" << t.offset.y
            << " lu=" << t.length.x << " lv=" << t.length.y << " tile=" << (int)t.tile << " seamless=" << (int)t.seamless
            << " side=" << t.side << "\n";
    }
    const std::pair<const char*, const AnimTrack*> tracks[] = {{"p", &node.animPos}, {"r", &node.animRot}, {"s", &node.animScale}};
    for (const auto& [ch, track] : tracks)
        for (const AnimKey& k : track->keys)
            out << indent << "  KEY ch=" << ch << " f=" << k.frame << " x=" << k.value.x << " y=" << k.value.y
                << " z=" << k.value.z << "\n";
    for (const auto& child : node.children()) writeNode(out, *child, depth + 1, meshRefs);
    out << indent << "}\n";
}

void writeMaterials(std::ostream& out, const std::vector<Material>& materials) {
    out << "MATERIALS {\n";
    for (const Material& m : materials) {
        out << "  MATERIAL name=" << quoted(m.name) << " br=" << m.baseColor.r << " bg=" << m.baseColor.g
            << " bb=" << m.baseColor.b << " metallic=" << m.metallic << " roughness=" << m.roughness
            << " er=" << m.emission.r << " eg=" << m.emission.g << " eb=" << m.emission.b << " tr=" << m.transparency.r
            << " tg=" << m.transparency.g << " tb=" << m.transparency.b << " ior=" << m.ior
            << " ns=" << m.normalStrength << " bs=" << m.bumpStrength;
        for (int i = 0; i < kTexSlotCount; ++i)
            if (!m.textures[i].empty()) out << " tex_" << texSlotCode((TexSlot)i) << '=' << quoted(m.textures[i]);
        if (!m.texBaseDir.empty()) out << " texdir=" << quoted(m.texBaseDir);
        out << " {\n";
        for (const ReflectLayer& l : m.layers)
            out << "    LAYER name=" << quoted(l.name) << " dist=" << l.distribution << " fresnel=" << l.fresnel
                << " preset=" << l.fresnelPreset << " strength=" << l.strength << " specular=" << l.specular
                << " rough=" << l.roughness << " ior=" << l.ior << " r=" << l.color.r << " g=" << l.color.g
                << " b=" << l.color.b << " transp=" << (int)l.transparencyLayer << " rtex=" << quoted(l.roughnessTex)
                << " ctex=" << quoted(l.colorTex) << "\n";
        out << "  }\n";
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
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    return g_readVersion >= 7 ? unescape(val) : val;
}

// Lee "clave=valor" mientras los haya (las lineas LIGHT, CAMERA, TEXTAG, LAYER).
template <class F>
bool readAttrs(Parser& p, const char* what, std::string& err, F&& onAttr) {
    while (!p.atEnd()) {
        const std::string& tok = p.peek();
        const size_t eq = tok.find('=');
        if (eq == std::string::npos || tok.front() == '"') break;
        const std::string t = p.next();
        try {
            onAttr(t.substr(0, eq), t.substr(eq + 1));
        } catch (const std::exception&) {
            err = std::string("valor invalido en ") + what + ": '" + t + "'";
            return false;
        }
    }
    return true;
}

bool parseMesh(Parser& p, std::shared_ptr<const MeshData>& out, std::string& err) {
    p.next(); // "MESH"
    size_t nv = 0, ni = 0;
    bool uv = false;
    while (!p.atEnd() && p.peek() != "{") {
        const std::string tok = p.next();
        try {
            if (tok.rfind("nv=", 0) == 0) nv = (size_t)std::stoul(tok.substr(3));
            else if (tok.rfind("ni=", 0) == 0) ni = (size_t)std::stoul(tok.substr(3));
            else if (tok == "uv=1") uv = true;
        } catch (const std::exception&) {
            err = "cabecera MESH invalida";
            return false;
        }
    }
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras MESH"; return false; }
    if (p.toks.size() - p.pos < nv * (uv ? 8 : 6) + ni) { err = "bloque MESH truncado"; return false; }
    auto mesh = std::make_shared<MeshData>();
    mesh->vertices.resize(nv);
    mesh->indices.resize(ni);
    try {
        for (size_t i = 0; i < nv; ++i) {
            Vertex& v = mesh->vertices[i];
            v.position.x = std::stof(p.next()); v.position.y = std::stof(p.next()); v.position.z = std::stof(p.next());
            v.normal.x = std::stof(p.next()); v.normal.y = std::stof(p.next()); v.normal.z = std::stof(p.next());
            if (uv) { v.uv.x = std::stof(p.next()); v.uv.y = std::stof(p.next()); }
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
    // Formato 6: FACES n=K { c1 c2 ... } (triangulos por poligono).
    if (!p.atEnd() && p.peek() == "FACES") {
        p.next();
        size_t nf = 0;
        while (!p.atEnd() && p.peek() != "{") {
            const std::string tok = p.next();
            try {
                if (tok.rfind("n=", 0) == 0) nf = (size_t)std::stoul(tok.substr(2));
            } catch (const std::exception&) {
                err = "cabecera FACES invalida";
                return false;
            }
        }
        if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras FACES"; return false; }
        if (p.toks.size() - p.pos < nf) { err = "bloque FACES truncado"; return false; }
        size_t sum = 0;
        try {
            for (size_t i = 0; i < nf; ++i) {
                const unsigned long c = std::stoul(p.next());
                if (c == 0) { err = "poligono sin triangulos en FACES"; return false; }
                mesh->faceTris.push_back((uint32_t)c);
                sum += c;
            }
        } catch (const std::exception&) {
            err = "valor numerico invalido en FACES";
            return false;
        }
        if (p.atEnd() || p.next() != "}") { err = "se esperaba '}' de cierre de FACES"; return false; }
        if (sum != ni / 3) mesh->faceTris.clear();  // no cuadra: se deducen, como sin FACES
    }
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
    while (!p.atEnd() && (p.peek() == "LIGHT" || p.peek() == "CAMERA" || p.peek() == "TEXTAG")) {
        const std::string kind = p.next();
        bool ok = true;
        if (kind == "LIGHT") {
            LightData& l = node.light;
            ok = readAttrs(p, "LIGHT", err, [&](const std::string& k, const std::string& v) {
                if (k == "kind") l.kind = (LightKind)std::clamp(std::stoi(v), 0, 9);
                else if (k == "r") l.color.r = std::stof(v);
                else if (k == "g") l.color.g = std::stof(v);
                else if (k == "b") l.color.b = std::stof(v);
                else if (k == "i") l.intensity = std::stof(v);
                else if (k == "outer") l.outerAngleDeg = std::stof(v);
                else if (k == "inner") l.innerAngleDeg = std::stof(v);
                else if (k == "shadow") l.shadow = std::stoi(v);
                else if (k == "falloff") l.falloff = std::stoi(v);
                else if (k == "radius") l.falloffRadius = std::stof(v);
                else if (k == "iradius") l.innerRadius = std::stof(v);
                else if (k == "shape") l.areaShape = std::stoi(v);
                else if (k == "ax") l.areaSize.x = std::stof(v);
                else if (k == "ay") l.areaSize.y = std::stof(v);
                else if (k == "az") l.areaSize.z = std::stof(v);
            });
        } else if (kind == "CAMERA") {
            CameraData& c = node.camera;
            ok = readAttrs(p, "CAMERA", err, [&](const std::string& k, const std::string& v) {
                if (k == "proj") c.projection = std::stoi(v);
                else if (k == "focal") c.focalMm = std::stof(v);
                else if (k == "sensor") c.sensorMm = std::stof(v);
                else if (k == "zoom") c.zoom = std::stof(v);
                else if (k == "target") c.target = unquote(v);
            });
        } else {
            TextureTagData& t = node.texTag;
            ok = readAttrs(p, "TEXTAG", err, [&](const std::string& k, const std::string& v) {
                if (k == "proj") t.projection = std::stoi(v);
                else if (k == "ou") t.offset.x = std::stof(v);
                else if (k == "ov") t.offset.y = std::stof(v);
                else if (k == "lu") t.length.x = std::stof(v);
                else if (k == "lv") t.length.y = std::stof(v);
                else if (k == "tile") t.tile = v != "0";
                else if (k == "seamless") t.seamless = v != "0";
                else if (k == "side") t.side = std::stoi(v);
            });
        }
        if (!ok) return false;
    }
    // Claves de animacion (formato 5): KEY ch=p|r|s f=N x= y= z=
    while (!p.atEnd() && p.peek() == "KEY") {
        p.next();
        std::string ch;
        int frame = 0;
        glm::vec3 v(0.0f);
        for (int i = 0; i < 5 && !p.atEnd(); ++i) {
            const std::string tok = p.next();
            const size_t eq = tok.find('=');
            if (eq == std::string::npos) { err = "clave de animacion malformada"; return false; }
            const std::string k = tok.substr(0, eq), val = tok.substr(eq + 1);
            try {
                if (k == "ch") ch = val;
                else if (k == "f") frame = std::stoi(val);
                else if (k == "x") v.x = std::stof(val);
                else if (k == "y") v.y = std::stof(val);
                else if (k == "z") v.z = std::stof(val);
            } catch (const std::exception&) {
                err = "valor invalido en una clave de animacion";
                return false;
            }
        }
        if (ch == "p") node.animPos.set(frame, v);
        else if (ch == "r") node.animRot.set(frame, v);
        else if (ch == "s") node.animScale.set(frame, v);
    }

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
            else if (key == "er") out.emission.r = std::stof(val);
            else if (key == "eg") out.emission.g = std::stof(val);
            else if (key == "eb") out.emission.b = std::stof(val);
            else if (key == "tr") out.transparency.r = std::stof(val);
            else if (key == "tg") out.transparency.g = std::stof(val);
            else if (key == "tb") out.transparency.b = std::stof(val);
            else if (key == "ior") out.ior = std::stof(val);
            else if (key == "ns") out.normalStrength = std::stof(val);
            else if (key == "bs") out.bumpStrength = std::stof(val);
            else if (key == "texdir") out.texBaseDir = unquote(val);
            else if (key.rfind("tex_", 0) == 0) {
                for (int i = 0; i < kTexSlotCount; ++i)
                    if (key.compare(4, std::string::npos, texSlotCode((TexSlot)i)) == 0) out.textures[i] = unquote(val);
            }
        } catch (const std::exception&) {
            err = "valor numerico invalido en '" + tok + "'";
            return false;
        }
    }
    if (p.atEnd() || p.next() != "{") { err = "se esperaba '{' tras los atributos de MATERIAL"; return false; }
    while (!p.atEnd() && p.peek() == "LAYER") {
        p.next();
        ReflectLayer l;
        const bool ok = readAttrs(p, "LAYER", err, [&](const std::string& k, const std::string& v) {
            if (k == "name") l.name = unquote(v);
            else if (k == "dist") l.distribution = std::stoi(v);
            else if (k == "fresnel") l.fresnel = std::stoi(v);
            else if (k == "preset") l.fresnelPreset = std::stoi(v);
            else if (k == "strength") l.strength = std::stof(v);
            else if (k == "specular") l.specular = std::stof(v);
            else if (k == "rough") l.roughness = std::stof(v);
            else if (k == "ior") l.ior = std::stof(v);
            else if (k == "r") l.color.r = std::stof(v);
            else if (k == "g") l.color.g = std::stof(v);
            else if (k == "b") l.color.b = std::stof(v);
            else if (k == "transp") l.transparencyLayer = v != "0";
            else if (k == "rtex") l.roughnessTex = unquote(v);
            else if (k == "ctex") l.colorTex = unquote(v);
        });
        if (!ok) return false;
        out.layers.push_back(std::move(l));
    }
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
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
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
    g_readVersion = kFormatVersion;
    try { g_readVersion = std::stoi(tokens[1]); } catch (...) {}
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
    int version = kFormatVersion;
    try { version = std::stoi(tokens[1]); } catch (...) {}
    if (version < 4) convertLegacyRotations(temp);

    root.setName(temp.name());
    root.primitive = temp.primitive;
    root.position = temp.position;
    root.rotationDeg = temp.rotationDeg;
    root.scale = temp.scale;
    root.editorVis = temp.editorVis;
    root.renderVis = temp.renderVis;
    root.materialIndex = temp.materialIndex;
    root.mesh = temp.mesh;
    root.light = temp.light;
    root.camera = temp.camera;
    root.texTag = temp.texTag;
    root.animPos = temp.animPos;
    root.animRot = temp.animRot;
    root.animScale = temp.animScale;
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
