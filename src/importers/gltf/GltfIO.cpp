#include "GltfIO.h"

#include "core/MeshData.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace iron3d {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// JSON minimo
// ---------------------------------------------------------------------------

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* get(const std::string& key) const {
        if (type != Obj) return nullptr;
        for (const auto& kv : o)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const Json* at(size_t i) const { return type == Arr && i < a.size() ? &a[i] : nullptr; }
    double num(const std::string& key, double def) const {
        const Json* j = get(key);
        return j && j->type == Num ? j->n : def;
    }
    int integer(const std::string& key, int def) const { return (int)num(key, def); }
    std::string str(const std::string& key) const {
        const Json* j = get(key);
        return j && j->type == Str ? j->s : std::string();
    }
    size_t size() const { return type == Arr ? a.size() : 0; }
};

struct JsonParser {
    const std::string& t;
    size_t i = 0;
    std::string err;
    explicit JsonParser(const std::string& text) : t(text) {}

    void ws() {
        while (i < t.size() && std::isspace((unsigned char)t[i])) ++i;
    }
    bool fail(const char* m) {
        if (err.empty()) err = m;
        return false;
    }
    bool parseValue(Json& out, int depth = 0) {
        if (depth > 64) return fail("JSON demasiado anidado");
        ws();
        if (i >= t.size()) return fail("JSON incompleto");
        const char c = t[i];
        if (c == '{') {
            out.type = Json::Obj;
            ++i;
            ws();
            if (i < t.size() && t[i] == '}') return ++i, true;
            while (true) {
                ws();
                Json key;
                if (i >= t.size() || t[i] != '"' || !parseString(key.s)) return fail("clave de JSON esperada");
                ws();
                if (i >= t.size() || t[i] != ':') return fail("':' esperado");
                ++i;
                Json v;
                if (!parseValue(v, depth + 1)) return false;
                out.o.emplace_back(std::move(key.s), std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') return ++i, true;
                return fail("'}' esperado");
            }
        }
        if (c == '[') {
            out.type = Json::Arr;
            ++i;
            ws();
            if (i < t.size() && t[i] == ']') return ++i, true;
            while (true) {
                Json v;
                if (!parseValue(v, depth + 1)) return false;
                out.a.push_back(std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') return ++i, true;
                return fail("']' esperado");
            }
        }
        if (c == '"') {
            out.type = Json::Str;
            return parseString(out.s);
        }
        if (t.compare(i, 4, "true") == 0) { out.type = Json::Bool; out.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { out.type = Json::Bool; out.b = false; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { out.type = Json::Null; i += 4; return true; }
        char* end = nullptr;
        out.n = std::strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return fail("valor de JSON no valido");
        out.type = Json::Num;
        i = (size_t)(end - t.c_str());
        return true;
    }
    bool parseString(std::string& out) {
        ++i; // comilla de apertura
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) break;
            const char e = t[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > t.size()) return fail("escape \\u incompleto");
                    const unsigned cp = (unsigned)std::strtoul(t.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                    else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: out += e; break; // " \ /
            }
        }
        if (i >= t.size()) return fail("cadena sin cerrar");
        ++i;
        return true;
    }
};

std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

std::string fnum(float v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", std::isfinite(v) ? v : 0.0f);
    return b;
}

std::vector<uint8_t> base64Decode(const std::string& in) {
    std::vector<uint8_t> out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+' || c == '-') d = 62;
        else if (c == '/' || c == '_') d = 63;
        else continue;
        val = (val << 6) + d;
        bits += 6;
        if (bits >= 0) {
            out.push_back((uint8_t)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

std::string percentDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---------------------------------------------------------------------------
// Importar
// ---------------------------------------------------------------------------

struct Doc {
    Json json;
    std::vector<std::vector<uint8_t>> buffers;
    fs::path dir;
};

// Lee un accesor como floats (n componentes por elemento).
bool readAccessor(const Doc& d, int index, std::vector<float>& out, int& comps) {
    const Json* accs = d.json.get("accessors");
    const Json* acc = accs ? accs->at((size_t)index) : nullptr;
    if (!acc) return false;
    const std::string type = acc->str("type");
    comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
    if (!comps) return false;
    const int ctype = acc->integer("componentType", 0);
    const size_t csize = (ctype == 5126 || ctype == 5125) ? 4 : (ctype == 5123 || ctype == 5122) ? 2 : (ctype == 5121 || ctype == 5120) ? 1 : 0;
    if (!csize) return false;
    const size_t count = (size_t)acc->num("count", 0);
    const bool normalized = acc->get("normalized") && acc->get("normalized")->b;
    out.assign(count * (size_t)comps, 0.0f);
    const Json* bvIdx = acc->get("bufferView");
    if (!bvIdx) return true; // accesor a ceros
    const Json* bvs = d.json.get("bufferViews");
    const Json* bv = bvs ? bvs->at((size_t)bvIdx->n) : nullptr;
    if (!bv) return false;
    const size_t bufIdx = (size_t)bv->num("buffer", 0);
    if (bufIdx >= d.buffers.size()) return false;
    const std::vector<uint8_t>& buf = d.buffers[bufIdx];
    const size_t off = (size_t)bv->num("byteOffset", 0) + (size_t)acc->num("byteOffset", 0);
    size_t stride = (size_t)bv->num("byteStride", 0);
    if (!stride) stride = csize * (size_t)comps;
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < comps; ++c) {
            const size_t at = off + i * stride + (size_t)c * csize;
            if (at + csize > buf.size()) return false;
            float v = 0;
            const uint8_t* p = buf.data() + at;
            switch (ctype) {
                case 5126: std::memcpy(&v, p, 4); break;
                case 5125: v = (float)rd32(p); break;
                case 5123: v = (float)(p[0] | (p[1] << 8)); if (normalized) v /= 65535.0f; break;
                case 5122: v = (float)(int16_t)(p[0] | (p[1] << 8)); if (normalized) v = std::max(v / 32767.0f, -1.0f); break;
                case 5121: v = (float)p[0]; if (normalized) v /= 255.0f; break;
                case 5120: v = (float)(int8_t)p[0]; if (normalized) v = std::max(v / 127.0f, -1.0f); break;
            }
            out[i * (size_t)comps + (size_t)c] = v;
        }
    }
    return true;
}

glm::mat4 nodeMatrix(const Json& n) {
    if (const Json* m = n.get("matrix")) {
        if (m->size() == 16) {
            glm::mat4 r(1.0f);
            for (int i = 0; i < 16; ++i) glm::value_ptr(r)[i] = (float)m->a[(size_t)i].n;
            return r;
        }
    }
    glm::vec3 t(0.0f), s(1.0f);
    glm::quat q(1, 0, 0, 0);
    if (const Json* v = n.get("translation")) if (v->size() == 3) t = {(float)v->a[0].n, (float)v->a[1].n, (float)v->a[2].n};
    if (const Json* v = n.get("scale")) if (v->size() == 3) s = {(float)v->a[0].n, (float)v->a[1].n, (float)v->a[2].n};
    if (const Json* v = n.get("rotation")) if (v->size() == 4) q = glm::quat((float)v->a[3].n, (float)v->a[0].n, (float)v->a[1].n, (float)v->a[2].n);
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), s);
}

struct ImportCtx {
    Doc* doc = nullptr;
    std::vector<int> materialMap; // glTF material -> indice global (-1 aun sin crear)
    std::vector<Material>* materials = nullptr;
    GltfReport* rep = nullptr;
    int depth = 0;
};

int materialFor(ImportCtx& cx, int gltfIndex) {
    if (gltfIndex < 0) return -1;
    if ((size_t)gltfIndex >= cx.materialMap.size()) return -1;
    if (cx.materialMap[(size_t)gltfIndex] >= 0) return cx.materialMap[(size_t)gltfIndex];
    const Json* mats = cx.doc->json.get("materials");
    const Json* m = mats ? mats->at((size_t)gltfIndex) : nullptr;
    Material out;
    out.name = m && !m->str("name").empty() ? m->str("name") : "Material " + std::to_string(gltfIndex);
    out.texBaseDir = cx.doc->dir.string();
    out.metallic = 1.0f; // por defecto de glTF si no hay pbrMetallicRoughness
    out.roughness = 1.0f;
    if (m) {
        if (const Json* pbr = m->get("pbrMetallicRoughness")) {
            if (const Json* c = pbr->get("baseColorFactor"))
                if (c->size() >= 3) out.baseColor = {(float)c->a[0].n, (float)c->a[1].n, (float)c->a[2].n};
            if (const Json* c = pbr->get("baseColorFactor"))
                if (c->size() == 4 && c->a[3].n < 1.0) { out.useTransparency = true; out.transparency = glm::vec3(1.0f - (float)c->a[3].n); }
            out.metallic = (float)pbr->num("metallicFactor", 1.0);
            out.roughness = (float)pbr->num("roughnessFactor", 1.0);
            if (const Json* bct = pbr->get("baseColorTexture")) {
                const int ti = bct->integer("index", -1);
                const Json* texs = cx.doc->json.get("textures");
                const Json* tex = texs ? texs->at((size_t)ti) : nullptr;
                const Json* imgs = cx.doc->json.get("images");
                const Json* img = tex && imgs ? imgs->at((size_t)tex->integer("source", -1)) : nullptr;
                if (img && !img->str("uri").empty() && img->str("uri").rfind("data:", 0) != 0)
                    out.textures[(int)TexSlot::Color] = percentDecode(img->str("uri"));
                else if (img)
                    cx.rep->warnings.push_back("Textura incrustada en el archivo: todavía no se extrae (material " + out.name + ")");
            }
        } else {
            out.metallic = 0.0f;
            out.roughness = 0.5f;
        }
        if (const Json* e = m->get("emissiveFactor"))
            if (e->size() == 3) {
                out.emission = {(float)e->a[0].n, (float)e->a[1].n, (float)e->a[2].n};
                if (out.emission.x + out.emission.y + out.emission.z > 0.0f) out.useLuminance = true;
            }
    }
    const int idx = (int)cx.materials->size();
    cx.materials->push_back(std::move(out));
    cx.materialMap[(size_t)gltfIndex] = idx;
    ++cx.rep->materials;
    return idx;
}

void buildMesh(ImportCtx& cx, const Json& meshJson, SceneNode& node) {
    auto mesh = std::make_shared<MeshData>();
    std::vector<MaterialPart> parts;
    const Json* prims = meshJson.get("primitives");
    for (size_t pi = 0; prims && pi < prims->size(); ++pi) {
        const Json& prim = prims->a[pi];
        if (prim.integer("mode", 4) != 4) {
            cx.rep->warnings.push_back("Primitivas que no son triángulos (líneas/puntos/tiras): omitidas");
            continue;
        }
        const Json* attr = prim.get("attributes");
        if (!attr || !attr->get("POSITION")) continue;
        std::vector<float> pos, nrm, uv, col;
        int cp = 0, cn = 0, cu = 0, cc = 0;
        if (!readAccessor(*cx.doc, attr->integer("POSITION", -1), pos, cp) || cp != 3) continue;
        const bool hasN = attr->get("NORMAL") && readAccessor(*cx.doc, attr->integer("NORMAL", -1), nrm, cn) && cn == 3;
        const bool hasUv = attr->get("TEXCOORD_0") && readAccessor(*cx.doc, attr->integer("TEXCOORD_0", -1), uv, cu) && cu == 2;
        const bool hasCol = attr->get("COLOR_0") && readAccessor(*cx.doc, attr->integer("COLOR_0", -1), col, cc) && cc >= 3;
        const size_t nv = pos.size() / 3;
        std::vector<uint32_t> idx;
        if (prim.get("indices")) {
            std::vector<float> f;
            int c = 0;
            if (!readAccessor(*cx.doc, prim.integer("indices", -1), f, c) || c != 1) continue;
            idx.reserve(f.size());
            for (float v : f) idx.push_back((uint32_t)v);
        } else {
            idx.resize(nv);
            for (size_t i = 0; i < nv; ++i) idx[i] = (uint32_t)i;
        }
        idx.resize(idx.size() / 3 * 3);
        bool bad = false;
        for (uint32_t v : idx) bad = bad || v >= nv;
        if (bad) {
            cx.rep->warnings.push_back("Índices fuera de rango en una primitiva: omitida");
            continue;
        }
        const uint32_t base = (uint32_t)mesh->vertices.size();
        for (size_t i = 0; i < nv; ++i) {
            Vertex v{};
            v.position = {pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
            v.color = hasCol ? glm::vec3(col[i * (size_t)cc], col[i * (size_t)cc + 1], col[i * (size_t)cc + 2]) : glm::vec3(1.0f);
            v.normal = hasN ? glm::vec3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]) : glm::vec3(0.0f);
            if (hasUv) v.uv = {uv[i * 2], uv[i * 2 + 1]};
            mesh->vertices.push_back(v);
        }
        if (!hasN) {
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                const glm::vec3& a = mesh->vertices[base + idx[i]].position;
                const glm::vec3& b = mesh->vertices[base + idx[i + 1]].position;
                const glm::vec3& c = mesh->vertices[base + idx[i + 2]].position;
                const glm::vec3 n = glm::cross(b - a, c - a);
                for (int k = 0; k < 3; ++k) mesh->vertices[base + idx[i + (size_t)k]].normal += n;
            }
            for (size_t i = 0; i < nv; ++i) {
                glm::vec3& n = mesh->vertices[base + i].normal;
                n = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : glm::vec3(0, 1, 0);
            }
        }
        MaterialPart part;
        part.firstIndex = (uint32_t)mesh->indices.size();
        part.indexCount = (uint32_t)idx.size();
        part.materialIndex = materialFor(cx, prim.integer("material", -1));
        for (uint32_t v : idx) mesh->indices.push_back(base + v);
        for (size_t t = 0; t < idx.size() / 3; ++t) mesh->faceTris.push_back(1);
        parts.push_back(part);
    }
    if (mesh->indices.empty()) return;
    updateBounds(*mesh);
    cx.rep->points += (long long)mesh->vertices.size();
    cx.rep->polygons += (long long)(mesh->indices.size() / 3);
    ++cx.rep->meshes;
    // Partes contiguas con el mismo material se funden.
    std::vector<MaterialPart> merged;
    for (const MaterialPart& p : parts) {
        if (!merged.empty() && merged.back().materialIndex == p.materialIndex) merged.back().indexCount += p.indexCount;
        else merged.push_back(p);
    }
    node.primitive = PrimitiveType::Mesh;
    node.materialIndex = merged.front().materialIndex;
    if (merged.size() > 1) node.materialParts = merged;
    node.mesh = std::move(mesh);
}

void importNode(ImportCtx& cx, int index, SceneNode& parent) {
    const Json* nodes = cx.doc->json.get("nodes");
    const Json* n = nodes ? nodes->at((size_t)index) : nullptr;
    if (!n || cx.depth > 200) return;
    SceneNode* node = parent.addChild(n->str("name").empty() ? "Nodo " + std::to_string(index) : n->str("name"));
    node->setLocalMatrix(nodeMatrix(*n));
    ++cx.rep->nodes;
    if (const Json* mi = n->get("mesh")) {
        const Json* meshes = cx.doc->json.get("meshes");
        if (const Json* m = meshes ? meshes->at((size_t)mi->n) : nullptr) buildMesh(cx, *m, *node);
    }
    if (const Json* ch = n->get("children")) {
        ++cx.depth;
        for (const Json& c : ch->a) importNode(cx, (int)c.n, *node);
        --cx.depth;
    }
}

} // namespace

bool isGltfPath(const fs::path& path) {
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e == ".glb" || e == ".gltf";
}

bool importGltf(const fs::path& path, SceneNode& root, std::vector<Material>& materials, GltfReport& report,
                std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    std::vector<uint8_t> file;
    if (!readFile(path, file)) return fail("No se pudo abrir el archivo.");
    Doc doc;
    doc.dir = path.parent_path();
    std::string jsonText;
    std::vector<uint8_t> glbBin;
    if (file.size() >= 12 && std::memcmp(file.data(), "glTF", 4) == 0) {
        size_t off = 12;
        while (off + 8 <= file.size()) {
            const uint32_t len = rd32(&file[off]);
            const uint32_t type = rd32(&file[off + 4]);
            off += 8;
            if (off + len > file.size()) return fail("Archivo .glb truncado.");
            if (type == 0x4E4F534Au) jsonText.assign((const char*)&file[off], len);
            else if (type == 0x004E4942u) glbBin.assign(file.begin() + (long long)off, file.begin() + (long long)(off + len));
            off += (len + 3u) & ~3u;
        }
    } else {
        jsonText.assign(file.begin(), file.end());
    }
    if (jsonText.empty()) return fail("No es un archivo glTF.");
    JsonParser jp(jsonText);
    if (!jp.parseValue(doc.json) || doc.json.type != Json::Obj) return fail("JSON de glTF no válido: " + jp.err);
    if (const Json* asset = doc.json.get("asset"))
        if (asset->str("version").rfind("2", 0) != 0) return fail("Solo se admite glTF 2.0.");
    // Buffers.
    const Json* bufs = doc.json.get("buffers");
    for (size_t i = 0; bufs && i < bufs->size(); ++i) {
        const std::string uri = bufs->a[i].str("uri");
        std::vector<uint8_t> data;
        if (uri.empty()) {
            data = glbBin;
        } else if (uri.rfind("data:", 0) == 0) {
            const size_t comma = uri.find(',');
            if (comma != std::string::npos) data = base64Decode(uri.substr(comma + 1));
        } else if (!readFile(doc.dir / fs::path(std::u8string(percentDecode(uri).begin(), percentDecode(uri).end())), data)) {
            report.warnings.push_back("No se encontró el buffer externo: " + uri);
        }
        doc.buffers.push_back(std::move(data));
    }
    ImportCtx cx;
    cx.doc = &doc;
    cx.materials = &materials;
    cx.rep = &report;
    const Json* mats = doc.json.get("materials");
    cx.materialMap.assign(mats ? mats->size() : 0, -1);
    std::vector<int> top;
    const Json* scenes = doc.json.get("scenes");
    if (scenes && scenes->size() > 0) {
        const Json* sc = scenes->at((size_t)doc.json.integer("scene", 0));
        if (!sc) sc = scenes->at(0);
        if (const Json* ns = sc->get("nodes")) for (const Json& v : ns->a) top.push_back((int)v.n);
    } else if (const Json* nodes = doc.json.get("nodes")) {
        std::vector<char> isChild(nodes->size(), 0);
        for (const Json& n : nodes->a)
            if (const Json* ch = n.get("children")) for (const Json& c : ch->a) if ((size_t)c.n < isChild.size()) isChild[(size_t)c.n] = 1;
        for (size_t i = 0; i < isChild.size(); ++i) if (!isChild[i]) top.push_back((int)i);
    }
    for (int t : top) importNode(cx, t, root);
    if (report.meshes == 0) return fail("El archivo no tiene mallas de triángulos.");
    if (doc.json.get("skins") || doc.json.get("animations"))
        report.warnings.push_back("Pieles y animaciones de glTF: todavía no se importan");
    return true;
}

// ---------------------------------------------------------------------------
// Exportar .glb
// ---------------------------------------------------------------------------

namespace {

struct ExpCtx {
    std::vector<uint8_t> bin;
    std::vector<std::string> bufferViews, accessors, meshes, nodes, materials, images, textures;
    std::map<int, int> matMap; // indice de escena -> glTF
    const std::vector<Material>* sceneMats = nullptr;
    GltfReport* rep = nullptr;
};

void pad4(std::vector<uint8_t>& v) { while (v.size() % 4) v.push_back(0); }

int addView(ExpCtx& cx, const void* data, size_t bytes, int target) {
    pad4(cx.bin);
    const size_t off = cx.bin.size();
    cx.bin.insert(cx.bin.end(), (const uint8_t*)data, (const uint8_t*)data + bytes);
    cx.bufferViews.push_back("{\"buffer\":0,\"byteOffset\":" + std::to_string(off) + ",\"byteLength\":" + std::to_string(bytes) +
                             ",\"target\":" + std::to_string(target) + "}");
    return (int)cx.bufferViews.size() - 1;
}

int addAccessor(ExpCtx& cx, int view, int ctype, size_t count, const char* type, const std::string& minmax = "") {
    cx.accessors.push_back("{\"bufferView\":" + std::to_string(view) + ",\"componentType\":" + std::to_string(ctype) +
                           ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" + minmax + "}");
    return (int)cx.accessors.size() - 1;
}

int exportMaterial(ExpCtx& cx, int sceneIndex) {
    if (sceneIndex < 0 || (size_t)sceneIndex >= cx.sceneMats->size()) return -1;
    auto it = cx.matMap.find(sceneIndex);
    if (it != cx.matMap.end()) return it->second;
    const Material& m = (*cx.sceneMats)[(size_t)sceneIndex];
    std::string j = "{\"name\":" + jsonEscape(m.name) + ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" + fnum(m.baseColor.x) + "," +
                    fnum(m.baseColor.y) + "," + fnum(m.baseColor.z) + "," +
                    fnum(m.useTransparency ? std::clamp(1.0f - m.transparency.x, 0.0f, 1.0f) : 1.0f) + "],\"metallicFactor\":" +
                    fnum(m.metallic) + ",\"roughnessFactor\":" + fnum(m.roughness);
    const std::string& tex = m.textures[(int)TexSlot::Color];
    if (!tex.empty()) {
        std::string uri = tex;
        std::replace(uri.begin(), uri.end(), '\\', '/');
        cx.images.push_back("{\"uri\":" + jsonEscape(uri) + "}");
        cx.textures.push_back("{\"source\":" + std::to_string(cx.images.size() - 1) + "}");
        j += ",\"baseColorTexture\":{\"index\":" + std::to_string(cx.textures.size() - 1) + "}";
    }
    j += "}";
    if (m.useLuminance) j += ",\"emissiveFactor\":[" + fnum(m.emission.x) + "," + fnum(m.emission.y) + "," + fnum(m.emission.z) + "]";
    if (m.useTransparency) j += ",\"alphaMode\":\"BLEND\"";
    j += "}";
    cx.materials.push_back(j);
    const int idx = (int)cx.materials.size() - 1;
    cx.matMap[sceneIndex] = idx;
    ++cx.rep->materials;
    return idx;
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

void exportNode(ExpCtx& cx, const SceneNode& n) {
    const MeshData mesh = meshOfNode(n);
    if (!mesh.vertices.empty() && mesh.indices.size() >= 3) {
        std::vector<float> pos, nrm, uv;
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (const Vertex& v : mesh.vertices) {
            pos.insert(pos.end(), {v.position.x, v.position.y, v.position.z});
            nrm.insert(nrm.end(), {v.normal.x, v.normal.y, v.normal.z});
            uv.insert(uv.end(), {v.uv.x, v.uv.y});
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        const size_t nv = mesh.vertices.size();
        const int aPos = addAccessor(cx, addView(cx, pos.data(), pos.size() * 4, 34962), 5126, nv, "VEC3",
                                     ",\"min\":[" + fnum(mn.x) + "," + fnum(mn.y) + "," + fnum(mn.z) + "],\"max\":[" + fnum(mx.x) + "," +
                                         fnum(mx.y) + "," + fnum(mx.z) + "]");
        const int aNrm = addAccessor(cx, addView(cx, nrm.data(), nrm.size() * 4, 34962), 5126, nv, "VEC3");
        const int aUv = addAccessor(cx, addView(cx, uv.data(), uv.size() * 4, 34962), 5126, nv, "VEC2");
        // Rangos de triangulos: partes y huecos con el material del nodo.
        struct Range { uint32_t first, count; int mat; };
        std::vector<Range> ranges;
        const uint32_t total = (uint32_t)(mesh.indices.size() / 3 * 3);
        std::vector<MaterialPart> parts = n.materialParts;
        std::sort(parts.begin(), parts.end(), [](const MaterialPart& a, const MaterialPart& b) { return a.firstIndex < b.firstIndex; });
        uint32_t cur = 0;
        for (const MaterialPart& p : parts) {
            if (p.firstIndex > cur) ranges.push_back({cur, p.firstIndex - cur, n.materialIndex});
            const uint32_t end = std::min(total, p.firstIndex + p.indexCount);
            if (end > std::max(cur, p.firstIndex)) ranges.push_back({std::max(cur, p.firstIndex), end - std::max(cur, p.firstIndex), p.materialIndex});
            cur = std::max(cur, end);
        }
        if (cur < total) ranges.push_back({cur, total - cur, n.materialIndex});
        std::string prims;
        for (const Range& r : ranges) {
            const int aIdx = addAccessor(cx, addView(cx, &mesh.indices[r.first], r.count * 4, 34963), 5125, r.count, "SCALAR");
            const int mat = exportMaterial(cx, r.mat);
            if (!prims.empty()) prims += ",";
            prims += "{\"attributes\":{\"POSITION\":" + std::to_string(aPos) + ",\"NORMAL\":" + std::to_string(aNrm) + ",\"TEXCOORD_0\":" +
                     std::to_string(aUv) + "},\"indices\":" + std::to_string(aIdx) + (mat >= 0 ? ",\"material\":" + std::to_string(mat) : "") + "}";
            cx.rep->polygons += r.count / 3;
        }
        cx.meshes.push_back("{\"name\":" + jsonEscape(n.name()) + ",\"primitives\":[" + prims + "]}");
        const glm::mat4 w = n.worldMatrix();
        std::string mat = "[";
        for (int i = 0; i < 16; ++i) mat += (i ? "," : "") + fnum(glm::value_ptr(w)[i]);
        mat += "]";
        cx.nodes.push_back("{\"name\":" + jsonEscape(n.name()) + ",\"mesh\":" + std::to_string(cx.meshes.size() - 1) + ",\"matrix\":" + mat + "}");
        ++cx.rep->nodes;
        ++cx.rep->meshes;
        cx.rep->points += (long long)nv;
    }
    for (const auto& c : n.children()) exportNode(cx, *c);
}

std::string joinList(const std::vector<std::string>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + v[i];
    return o;
}

} // namespace

bool exportGlb(const fs::path& path, const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
               GltfReport& report, std::string* error) {
    ExpCtx cx;
    cx.sceneMats = &materials;
    cx.rep = &report;
    for (const SceneNode* r : roots) exportNode(cx, *r);
    if (cx.meshes.empty()) {
        if (error) *error = "No hay objetos con malla que exportar.";
        return false;
    }
    std::string sceneNodes;
    for (size_t i = 0; i < cx.nodes.size(); ++i) sceneNodes += (i ? "," : "") + std::to_string(i);
    pad4(cx.bin);
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Iron3D Studio\"},\"scene\":0,\"scenes\":[{\"nodes\":[" + sceneNodes +
                       "]}],\"nodes\":[" + joinList(cx.nodes) + "],\"meshes\":[" + joinList(cx.meshes) + "]";
    if (!cx.materials.empty()) json += ",\"materials\":[" + joinList(cx.materials) + "]";
    if (!cx.textures.empty()) json += ",\"textures\":[" + joinList(cx.textures) + "],\"images\":[" + joinList(cx.images) + "]";
    json += ",\"accessors\":[" + joinList(cx.accessors) + "],\"bufferViews\":[" + joinList(cx.bufferViews) +
            "],\"buffers\":[{\"byteLength\":" + std::to_string(cx.bin.size()) + "}]}";
    while (json.size() % 4) json += ' ';
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "No se pudo crear el archivo.";
        return false;
    }
    auto w32 = [&](uint32_t v) { const char b[4] = {(char)(v & 255), (char)((v >> 8) & 255), (char)((v >> 16) & 255), (char)((v >> 24) & 255)}; out.write(b, 4); };
    const uint32_t totalLen = 12 + 8 + (uint32_t)json.size() + 8 + (uint32_t)cx.bin.size();
    out.write("glTF", 4);
    w32(2);
    w32(totalLen);
    w32((uint32_t)json.size());
    w32(0x4E4F534Au);
    out.write(json.data(), (std::streamsize)json.size());
    w32((uint32_t)cx.bin.size());
    w32(0x004E4942u);
    out.write((const char*)cx.bin.data(), (std::streamsize)cx.bin.size());
    return out.good();
}

} // namespace iron3d
