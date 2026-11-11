#include "ShaderTree.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace iron3d {

const char* shaderKindName(ShaderKind k) {
    switch (k) {
        case ShaderKind::None: return "(ninguno)";
        case ShaderKind::Bitmap: return "Imagen";
        case ShaderKind::Color: return "Color";
        case ShaderKind::Filter: return "Filtro";
        case ShaderKind::Colorizer: return "Colorizador";
        case ShaderKind::Layer: return "Capa";
        case ShaderKind::Gradient: return "Degradado";
        case ShaderKind::Other: return "Otro";
    }
    return "?";
}

const ShaderNode* ShaderNode::firstBitmap() const {
    if (kind == ShaderKind::Bitmap) return this;
    for (const ShaderNode& c : children)
        if (const ShaderNode* b = c.firstBitmap()) return b;
    return nullptr;
}

ShaderNode* ShaderNode::firstBitmap() {
    return const_cast<ShaderNode*>(static_cast<const ShaderNode*>(this)->firstBitmap());
}

bool ShaderNode::hasUnsupported() const {
    if (kind == ShaderKind::Other) return true;
    for (const ShaderLayer& l : layers)
        if (l.active && (l.type == 9 || l.type == 10)) return true; // distorsion / transformar
    for (const ShaderNode& c : children)
        if (c.hasUnsupported()) return true;
    return false;
}

// --- Texto ----------------------------------------------------------------------
// Tokens separados por espacios: numeros tal cual, cadenas "#<bytes>:<texto>"
// (con longitud: no hay nada que escapar), listas entre [ ].
// Nodo: [ clave=valor ... knots=[ [r g b pos bias interp] ... ]
//         layers=[ [type act mode blend flags child a b c colorize cycle [knots]] ... ]
//         ch=[ nodo ... ] ]

namespace {

void putStr(std::ostringstream& o, const std::string& s) { o << '#' << s.size() << ':' << s; }

void putKnots(std::ostringstream& o, const std::vector<GradientKnot>& ks) {
    o << "[ ";
    for (const GradientKnot& k : ks)
        o << "[ " << k.color.r << ' ' << k.color.g << ' ' << k.color.b << ' ' << k.pos << ' ' << k.bias << ' '
          << k.interpolation << " ] ";
    o << "]";
}

void putNode(std::ostringstream& o, const ShaderNode& n) {
    o << "[ k=" << (int)n.kind << " t=" << n.c4dType << " n=";
    putStr(o, n.name);
    switch (n.kind) {
        case ShaderKind::Bitmap:
            o << " p=";
            putStr(o, n.path);
            o << " samp=" << n.sampling;
            break;
        case ShaderKind::Color:
            o << " col=" << n.color.r << ',' << n.color.g << ',' << n.color.b << " br=" << n.brightness;
            break;
        case ShaderKind::Filter:
            o << " h=" << n.hue << " s=" << n.saturation << " l=" << n.lightness << " cz=" << (int)n.colorize
              << " fb=" << n.fBrightness << " fc=" << n.contrast << " g=" << n.gamma << " clip=" << (int)n.clipping
              << " lo=" << n.lowClip << " hi=" << n.highClip;
            break;
        case ShaderKind::Colorizer:
        case ShaderKind::Gradient:
            o << " in=" << n.input << " cyc=" << (int)n.cycle << " gt=" << n.gradientType << " ang=" << n.angle
              << " knots=";
            putKnots(o, n.gradient);
            break;
        case ShaderKind::Layer:
            o << " layers=[ ";
            for (const ShaderLayer& l : n.layers) {
                o << "[ " << l.type << ' ' << (int)l.active << ' ' << l.mode << ' ' << l.blend << ' ' << l.flags << ' '
                  << l.child << ' ' << l.a << ' ' << l.b << ' ' << l.c << ' ' << l.colorize << ' ' << (int)l.cycle
                  << ' ';
                putKnots(o, l.gradient);
                o << " ] ";
            }
            o << "]";
            break;
        default: break;
    }
    if (!n.children.empty()) {
        o << " ch=[ ";
        for (const ShaderNode& c : n.children) {
            putNode(o, c);
            o << ' ';
        }
        o << "]";
    }
    o << " ]";
}

struct Reader {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    void ws() {
        while (i < s.size() && s[i] == ' ') ++i;
    }
    bool lit(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool peekIs(char c) {
        ws();
        return i < s.size() && s[i] == c;
    }
    std::string word() { // hasta espacio, '=' o ']'
        ws();
        size_t st = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '=' && s[i] != ']') ++i;
        return s.substr(st, i - st);
    }
    double num() {
        std::string w = word();
        if (w.empty()) ok = false;
        return std::strtod(w.c_str(), nullptr);
    }
    std::string str() {
        ws();
        if (i >= s.size() || s[i] != '#') {
            ok = false;
            return {};
        }
        ++i;
        size_t colon = s.find(':', i);
        if (colon == std::string::npos) {
            ok = false;
            return {};
        }
        const size_t len = (size_t)std::strtoull(s.substr(i, colon - i).c_str(), nullptr, 10);
        i = colon + 1;
        if (i + len > s.size()) {
            ok = false;
            return {};
        }
        std::string out = s.substr(i, len);
        i += len;
        return out;
    }
    glm::vec3 vec3() {
        std::string w = word();
        glm::vec3 v(1.0f);
        const char* q = w.c_str();
        char* end = nullptr;
        for (int k = 0; k < 3; ++k) {
            v[k] = std::strtof(q, &end);
            if (end == q) { ok = false; break; }
            q = (*end == ',') ? end + 1 : end;
        }
        return v;
    }
    std::vector<GradientKnot> knots() {
        std::vector<GradientKnot> ks;
        if (!lit('[')) {
            ok = false;
            return ks;
        }
        while (ok && lit('[')) {
            GradientKnot k;
            k.color.r = (float)num();
            k.color.g = (float)num();
            k.color.b = (float)num();
            k.pos = (float)num();
            k.bias = (float)num();
            k.interpolation = (int)num();
            if (!lit(']')) ok = false;
            ks.push_back(k);
        }
        if (!lit(']')) ok = false;
        return ks;
    }
    bool node(ShaderNode& n, int depth) {
        if (depth > 16 || !lit('[')) return false;
        while (ok && !peekIs(']')) {
            const std::string key = word();
            if (key.empty() || !lit('=')) return false;
            if (key == "k") n.kind = (ShaderKind)(int)num();
            else if (key == "t") n.c4dType = (int)num();
            else if (key == "n") n.name = str();
            else if (key == "p") n.path = str();
            else if (key == "samp") n.sampling = (int)num();
            else if (key == "col") n.color = vec3();
            else if (key == "br") n.brightness = (float)num();
            else if (key == "h") n.hue = (float)num();
            else if (key == "s") n.saturation = (float)num();
            else if (key == "l") n.lightness = (float)num();
            else if (key == "cz") n.colorize = num() != 0.0;
            else if (key == "fb") n.fBrightness = (float)num();
            else if (key == "fc") n.contrast = (float)num();
            else if (key == "g") n.gamma = (float)num();
            else if (key == "clip") n.clipping = num() != 0.0;
            else if (key == "lo") n.lowClip = (float)num();
            else if (key == "hi") n.highClip = (float)num();
            else if (key == "in") n.input = (int)num();
            else if (key == "cyc") n.cycle = num() != 0.0;
            else if (key == "gt") n.gradientType = (int)num();
            else if (key == "ang") n.angle = (float)num();
            else if (key == "knots") n.gradient = knots();
            else if (key == "layers") {
                if (!lit('[')) return false;
                while (ok && lit('[')) {
                    ShaderLayer l;
                    l.type = (int)num();
                    l.active = num() != 0.0;
                    l.mode = (int)num();
                    l.blend = (float)num();
                    l.flags = (int)num();
                    l.child = (int)num();
                    l.a = (float)num();
                    l.b = (float)num();
                    l.c = (float)num();
                    l.colorize = (int)num();
                    l.cycle = num() != 0.0;
                    l.gradient = knots();
                    if (!lit(']')) return false;
                    n.layers.push_back(std::move(l));
                }
                if (!lit(']')) return false;
            } else if (key == "ch") {
                if (!lit('[')) return false;
                while (ok && peekIs('[')) {
                    ShaderNode c;
                    if (!node(c, depth + 1)) return false;
                    n.children.push_back(std::move(c));
                }
                if (!lit(']')) return false;
            } else {
                return false; // clave desconocida: mejor no adivinar
            }
        }
        return ok && lit(']');
    }
};

} // namespace

std::string encodeShaderTree(const ShaderNode& n) {
    if (n.empty()) return {};
    std::ostringstream o;
    o.imbue(std::locale::classic());
    putNode(o, n);
    return o.str();
}

bool decodeShaderTree(const std::string& text, ShaderNode& out) {
    out = {};
    if (text.empty()) return true;
    Reader r{text};
    ShaderNode n;
    if (!r.node(n, 0) || !r.ok) return false;
    out = std::move(n);
    return true;
}

} // namespace iron3d
