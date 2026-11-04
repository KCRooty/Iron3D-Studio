#include "UiPreset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace iron3d {

namespace {

glm::vec3 hex(unsigned rgb) {
    return glm::vec3((float)((rgb >> 16) & 0xFF) / 255.0f, (float)((rgb >> 8) & 0xFF) / 255.0f,
                     (float)(rgb & 0xFF) / 255.0f);
}

// Colores de cada programa: muestreados de las capturas de referencia de Core
// (Blender 4.5, Maya, Source Filmmaker) y, para 3ds Max, de su tema oscuro
// por defecto. Son aproximaciones, no valores oficiales.
std::vector<UiPreset> makeBuiltins() {
    std::vector<UiPreset> v;

    UiPreset c4d; // todo por defecto = el Iron3D de siempre
    v.push_back(c4d);

    UiPreset b;
    b.id = "blender";
    b.name = "Blender";
    b.theme.panelBg = hex(0x303030);
    b.theme.fieldBg = hex(0x1D1D1D);
    b.theme.accent = hex(0x4772B3);
    b.theme.toolBlue = hex(0x4772B3);
    b.theme.text = hex(0xE6E6E6);
    b.theme.selection = hex(0xFFA028);
    b.theme.viewportBg = hex(0x3F3F3F);
    b.layout.arrangement = Arrangement::Blender;
    b.layout.toolbarH = 32.0f;
    b.layout.leftToolbarW = 38.0f;
    b.layout.sideFrac = 0.22f;
    b.layout.sideMin = 290.0f;
    b.layout.sideMax = 430.0f;
    b.layout.objFrac = 0.30f;
    b.layout.shelfFrac = 0.15f;
    b.layout.shelfMin = 165.0f;
    b.layout.shelfMax = 200.0f;
    b.layout.timelineH = 50.0f;
    b.layout.verticalTabs = false;
    b.style = {4.0f, 0.0f, 5.0f, 4.0f, 4.0f, 6.0f, 3.0f};
    v.push_back(b);

    UiPreset m;
    m.id = "maya";
    m.name = "Maya";
    m.theme.panelBg = hex(0x444444);
    m.theme.fieldBg = hex(0x2B2B2B);
    m.theme.accent = hex(0x5285A6);
    m.theme.toolBlue = hex(0x5285A6);
    m.theme.text = hex(0xCCCCCC);
    m.theme.selection = hex(0x43FFA3);
    m.theme.viewportBg = hex(0x5E6670);
    m.layout.arrangement = Arrangement::Maya;
    m.layout.toolbarH = 40.0f;
    m.layout.leftToolbarW = 36.0f;
    m.layout.outlinerW = 230.0f;
    m.layout.sideFrac = 0.20f;
    m.layout.sideMin = 290.0f;
    m.layout.sideMax = 400.0f;
    m.layout.shelfFrac = 0.15f;
    m.layout.shelfMin = 165.0f;
    m.layout.shelfMax = 200.0f;
    m.layout.timelineH = 52.0f;
    m.layout.verticalTabs = true;
    m.style = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 2.0f};
    v.push_back(m);

    UiPreset x;
    x.id = "max";
    x.name = "3ds Max";
    x.theme.panelBg = hex(0x3A3A3A);
    x.theme.fieldBg = hex(0x262626);
    x.theme.accent = hex(0x4EA2E0);
    x.theme.toolBlue = hex(0x3D86C6);
    x.theme.text = hex(0xDCDCDC);
    x.theme.selection = hex(0xF2F2F2);
    x.theme.viewportBg = hex(0x444444);
    x.layout.arrangement = Arrangement::Max;
    x.layout.toolbarH = 36.0f;
    x.layout.leftToolbarW = 36.0f;
    x.layout.outlinerW = 210.0f;
    x.layout.sideFrac = 0.20f;
    x.layout.sideMin = 290.0f;
    x.layout.sideMax = 360.0f;
    x.layout.shelfFrac = 0.14f;
    x.layout.shelfMin = 165.0f;
    x.layout.shelfMax = 200.0f;
    x.layout.timelineH = 52.0f;
    x.layout.verticalTabs = false;
    x.style = {2.0f, 0.0f, 2.0f, 2.0f, 0.0f, 6.0f, 3.0f};
    v.push_back(x);

    UiPreset s;
    s.id = "sfm";
    s.name = "Source Filmmaker";
    s.theme.panelBg = hex(0x262626);
    s.theme.fieldBg = hex(0x1C1C1C);
    s.theme.accent = hex(0x4A8FDA);
    s.theme.toolBlue = hex(0x3C78B8);
    s.theme.text = hex(0xCFCFCF);
    s.theme.selection = hex(0x6FB2FF);
    s.theme.viewportBg = hex(0x0A0A0A);
    s.layout.arrangement = Arrangement::SFM;
    s.layout.toolbarH = 34.0f;
    s.layout.leftToolbarW = 36.0f;
    s.layout.sideFrac = 0.22f;
    s.layout.sideMin = 290.0f;
    s.layout.sideMax = 420.0f;
    s.layout.objFrac = 0.60f;
    s.layout.shelfFrac = 0.13f;
    s.layout.shelfMin = 165.0f;
    s.layout.shelfMax = 200.0f;
    s.layout.timelineFrac = 0.30f;
    s.layout.verticalTabs = false;
    s.style = {2.0f, 0.0f, 2.0f, 2.0f, 2.0f, 6.0f, 3.0f};
    v.push_back(s);
    return v;
}

std::string vec3Code(const glm::vec3& c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", c.x, c.y, c.z);
    return buf;
}

bool parseVec3(const std::string& val, glm::vec3& out) {
    std::istringstream iss(val);
    std::string comp;
    glm::vec3 v(0.0f);
    for (int i = 0; i < 3; ++i) {
        if (!std::getline(iss, comp, ',')) return false;
        try {
            v[i] = std::stof(comp);
        } catch (...) {
            return false;
        }
    }
    out = v;
    return true;
}

bool parseFloat(const std::string& val, float& out) {
    try {
        out = std::stof(val);
        return true;
    } catch (...) {
        return false;
    }
}

const char* arrangementCode(Arrangement a) {
    switch (a) {
        case Arrangement::Blender: return "blender";
        case Arrangement::Maya: return "maya";
        case Arrangement::Max: return "max";
        case Arrangement::SFM: return "sfm";
        case Arrangement::C4D: break;
    }
    return "c4d";
}

Arrangement arrangementFromCode(const std::string& c) {
    if (c == "blender") return Arrangement::Blender;
    if (c == "maya") return Arrangement::Maya;
    if (c == "max") return Arrangement::Max;
    if (c == "sfm") return Arrangement::SFM;
    return Arrangement::C4D;
}

// Nombre de archivo seguro: letras, numeros, espacio, guion y guion bajo
// (los bytes UTF-8 >= 0x80 pasan, para acentos).
std::string safeFileName(const std::string& name) {
    std::string out;
    for (char ch : name) {
        const unsigned char u = (unsigned char)ch;
        if (std::isalnum(u) || ch == ' ' || ch == '-' || ch == '_' || u >= 0x80) out += ch;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

constexpr const char* kPresetHeader = "IRON3D_UIPRESET 1";

// Ruta desde texto UTF-8 (std::filesystem::u8path esta obsoleto en C++20).
std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

} // namespace

const char* arrangementName(Arrangement a) {
    switch (a) {
        case Arrangement::Blender: return "Blender";
        case Arrangement::Maya: return "Maya";
        case Arrangement::Max: return "3ds Max";
        case Arrangement::SFM: return "Source Filmmaker";
        case Arrangement::C4D: break;
    }
    return "Cinema 4D";
}

const PanelNames& panelNames(Arrangement a) {
    static const PanelNames kC4D{"Gestor de Objetos", "Atributos", "Línea de Tiempo", "Gestor de Materiales",
                                 "Coordenadas", "Perspectiva", "Entorno:",
                                 {"Objetos", "Tomas", "Navegador de Contenido", "Estructura"}, 4,
                                 {"Atributos", "Capas", nullptr}, 2};
    static const PanelNames kBlender{"Esquema", "Propiedades", "Línea de tiempo", "Materiales", "Transformar",
                                     "Vista 3D", "Espacio de trabajo:",
                                     {"Esquema", nullptr, nullptr, nullptr}, 1,
                                     {"Propiedades", nullptr, nullptr}, 1};
    static const PanelNames kMaya{"Outliner", "Channel Box / Layer Editor", "Time Slider", "Hypershade",
                                  "Transform Settings", "persp", "Workspace:",
                                  {"Outliner", nullptr, nullptr, nullptr}, 1,
                                  {"Channel Box / Layer Editor", "Attribute Editor", "Modeling Toolkit"}, 3};
    static const PanelNames kMax{"Scene Explorer", "Command Panel", "Time Slider", "Material Editor",
                                 "Transform Type-In", "[+] [Perspective]", "Workspaces:",
                                 {"Scene Explorer", nullptr, nullptr, nullptr}, 1,
                                 {"Command Panel", nullptr, nullptr}, 1};
    static const PanelNames kSFM{"Animation Set Editor", "Element Viewer", "Timeline", "Materials", "Transform",
                                 "Primary Viewport", "Layout:",
                                 {"Animation Set Editor", nullptr, nullptr, nullptr}, 1,
                                 {"Element Viewer", nullptr, nullptr}, 1};
    switch (a) {
        case Arrangement::Blender: return kBlender;
        case Arrangement::Maya: return kMaya;
        case Arrangement::Max: return kMax;
        case Arrangement::SFM: return kSFM;
        case Arrangement::C4D: break;
    }
    return kC4D;
}

const std::vector<UiPreset>& builtinPresets() {
    static const std::vector<UiPreset> v = makeBuiltins();
    return v;
}

const UiPreset* findBuiltinPreset(const std::string& id) {
    for (const UiPreset& p : builtinPresets())
        if (p.id == id) return &p;
    return nullptr;
}

void writePresetKeys(std::ostream& out, const UiPreset& p) {
    out << "ui.preset=" << p.id << "\n";
    out << "ui.name=" << p.name << "\n";
    out << "theme.panelBg=" << vec3Code(p.theme.panelBg) << "\n";
    out << "theme.fieldBg=" << vec3Code(p.theme.fieldBg) << "\n";
    out << "theme.accent=" << vec3Code(p.theme.accent) << "\n";
    out << "theme.toolBlue=" << vec3Code(p.theme.toolBlue) << "\n";
    out << "theme.text=" << vec3Code(p.theme.text) << "\n";
    out << "theme.selection=" << vec3Code(p.theme.selection) << "\n";
    out << "theme.viewportBg=" << vec3Code(p.theme.viewportBg) << "\n";
    const LayoutSpec& l = p.layout;
    out << "layout.arrangement=" << arrangementCode(l.arrangement) << "\n";
    out << "layout.toolbarH=" << l.toolbarH << "\n";
    out << "layout.leftToolbarW=" << l.leftToolbarW << "\n";
    out << "layout.tabsW=" << l.tabsW << "\n";
    out << "layout.sideFrac=" << l.sideFrac << "\n";
    out << "layout.sideMin=" << l.sideMin << "\n";
    out << "layout.sideMax=" << l.sideMax << "\n";
    out << "layout.outlinerW=" << l.outlinerW << "\n";
    out << "layout.objFrac=" << l.objFrac << "\n";
    out << "layout.shelfFrac=" << l.shelfFrac << "\n";
    out << "layout.shelfMin=" << l.shelfMin << "\n";
    out << "layout.shelfMax=" << l.shelfMax << "\n";
    out << "layout.timelineH=" << l.timelineH << "\n";
    out << "layout.timelineFrac=" << l.timelineFrac << "\n";
    out << "layout.verticalTabs=" << (l.verticalTabs ? 1 : 0) << "\n";
    const StyleMetrics& s = p.style;
    out << "style.frameRounding=" << s.frameRounding << "\n";
    out << "style.windowRounding=" << s.windowRounding << "\n";
    out << "style.popupRounding=" << s.popupRounding << "\n";
    out << "style.grabRounding=" << s.grabRounding << "\n";
    out << "style.tabRounding=" << s.tabRounding << "\n";
    out << "style.framePadX=" << s.framePadX << "\n";
    out << "style.framePadY=" << s.framePadY << "\n";
}

bool readPresetKey(UiPreset& p, const std::string& key, const std::string& val) {
    if (key == "ui.preset") {
        // Un id integrado parte de ese preset entero; las claves que vengan
        // detras (siempre van detras: writePresetKeys lo escribe primero) lo
        // ajustan. Asi un archivo con solo "ui.preset=maya" ya es Maya.
        if (const UiPreset* b = findBuiltinPreset(val)) p = *b;
        p.id = val;
        return true;
    }
    if (key == "ui.name") {
        p.name = val;
        return true;
    }
    struct V3 {
        const char* k;
        glm::vec3* v;
    };
    const V3 v3[] = {{"theme.panelBg", &p.theme.panelBg},   {"theme.fieldBg", &p.theme.fieldBg},
                     {"theme.accent", &p.theme.accent},     {"theme.toolBlue", &p.theme.toolBlue},
                     {"theme.text", &p.theme.text},         {"theme.selection", &p.theme.selection},
                     {"theme.viewportBg", &p.theme.viewportBg}};
    for (const V3& e : v3)
        if (key == e.k) {
            parseVec3(val, *e.v);
            return true;
        }
    if (key == "layout.arrangement") {
        p.layout.arrangement = arrangementFromCode(val);
        return true;
    }
    if (key == "layout.verticalTabs") {
        p.layout.verticalTabs = val == "1";
        return true;
    }
    LayoutSpec& l = p.layout;
    StyleMetrics& s = p.style;
    struct F {
        const char* k;
        float* v;
    };
    const F fl[] = {{"layout.toolbarH", &l.toolbarH},           {"layout.leftToolbarW", &l.leftToolbarW},
                    {"layout.tabsW", &l.tabsW},                 {"layout.sideFrac", &l.sideFrac},
                    {"layout.sideMin", &l.sideMin},             {"layout.sideMax", &l.sideMax},
                    {"layout.outlinerW", &l.outlinerW},         {"layout.objFrac", &l.objFrac},
                    {"layout.shelfFrac", &l.shelfFrac},         {"layout.shelfMin", &l.shelfMin},
                    {"layout.shelfMax", &l.shelfMax},           {"layout.timelineH", &l.timelineH},
                    {"layout.timelineFrac", &l.timelineFrac},   {"style.frameRounding", &s.frameRounding},
                    {"style.windowRounding", &s.windowRounding}, {"style.popupRounding", &s.popupRounding},
                    {"style.grabRounding", &s.grabRounding},    {"style.tabRounding", &s.tabRounding},
                    {"style.framePadX", &s.framePadX},          {"style.framePadY", &s.framePadY}};
    for (const F& e : fl)
        if (key == e.k) {
            parseFloat(val, *e.v);
            return true;
        }
    return false;
}

std::vector<UiPreset> loadUserPresets(const std::filesystem::path& dir) {
    std::vector<UiPreset> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".i3dui") continue;
        std::ifstream f(entry.path(), std::ios::binary);
        std::string line;
        if (!std::getline(f, line) || line.rfind("IRON3D_UIPRESET", 0) != 0) continue;
        UiPreset p;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            readPresetKey(p, line.substr(0, eq), line.substr(eq + 1));
        }
        // El nombre manda el del archivo (es lo que se borra/sobrescribe).
        const auto stem = entry.path().stem().u8string();
        p.name = std::string(stem.begin(), stem.end());
        p.id = "user:" + p.name;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const UiPreset& a, const UiPreset& b) { return a.name < b.name; });
    return out;
}

bool saveUserPreset(const std::filesystem::path& dir, const UiPreset& preset) {
    const std::string file = safeFileName(preset.name);
    if (file.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    UiPreset p = preset;
    p.name = file;
    p.id = "user:" + file;
    std::ofstream f(dir / utf8Path(file + ".i3dui"), std::ios::binary);
    if (!f) return false;
    f << kPresetHeader << "\n";
    writePresetKeys(f, p);
    return (bool)f;
}

bool deleteUserPreset(const std::filesystem::path& dir, const std::string& name) {
    const std::string file = safeFileName(name);
    if (file.empty()) return false;
    std::error_code ec;
    return std::filesystem::remove(dir / utf8Path(file + ".i3dui"), ec);
}

LayoutRects computeLayout(const LayoutSpec& L, ImVec2 origin, ImVec2 size, float menuH, float statusH) {
    LayoutRects r;
    const float X = origin.x, Y = origin.y, W = size.x, H = size.y;
    const float top = Y + menuH;
    const float contentTop = top + L.toolbarH;
    const float statusTop = Y + H - statusH;
    const float sideW = std::clamp(W * L.sideFrac, L.sideMin, std::max(L.sideMin, L.sideMax));
    const float shelfH = std::clamp(H * L.shelfFrac, L.shelfMin, std::max(L.shelfMin, L.shelfMax));
    const float timelineH = L.timelineFrac > 0.0f ? std::floor(H * L.timelineFrac) : L.timelineH;
    const float tabsW = L.verticalTabs ? L.tabsW : 0.0f;
    auto rect = [](float x, float y, float w, float h) {
        return PanelRect{ImVec2(x, y), ImVec2(std::max(1.0f, w), std::max(1.0f, h))};
    };
    // Fila Materiales | Coordenadas dentro de [x, x+w] (mismos numeros que C4D).
    auto shelf = [&](float x, float y, float w) {
        const float coordW = std::clamp(w * 0.36f, 300.0f, 430.0f);
        r.materials = rect(x, y, w - coordW, shelfH);
        r.coords = rect(x + w - coordW, y, coordW, shelfH);
    };
    // Columna con Objetos arriba y Atributos abajo (+ pestanas verticales).
    auto stacked = [&](float x, float y, float w, float h) {
        const float objH = std::floor(h * L.objFrac);
        const float pw = w - tabsW;
        r.objects = rect(x, y, pw, objH);
        r.attributes = rect(x, y + objH, pw, h - objH);
        if (tabsW > 0.0f) {
            r.objTabs = rect(x + pw, y, tabsW, objH);
            r.attrTabs = rect(x + pw, y + objH, tabsW, h - objH);
        }
    };

    r.top = rect(X, top, W, L.toolbarH);
    r.status = rect(X, statusTop, W, statusH);

    switch (L.arrangement) {
        case Arrangement::C4D: {
            // Layout fijo del "Entorno de Arranque" de C4D (el de siempre).
            const float bottomTop = statusTop - shelfH;
            const float timelineTop = bottomTop - timelineH;
            const float rightX = X + W - sideW;
            const float leftAreaW = W - sideW;
            r.left = rect(X, contentTop, L.leftToolbarW, timelineTop - contentTop);
            r.viewport = rect(X + L.leftToolbarW, contentTop, leftAreaW - L.leftToolbarW, timelineTop - contentTop);
            stacked(rightX, contentTop, sideW, statusTop - contentTop);
            r.timeline = rect(X, timelineTop, leftAreaW, timelineH);
            shelf(X, bottomTop, leftAreaW);
            break;
        }
        case Arrangement::Blender: {
            // Esquema + Propiedades a la derecha a toda altura; bajo la vista
            // 3D la fila de editores y la Linea de tiempo como franja mas baja.
            const float timelineTop = statusTop - timelineH;
            const float bottomTop = timelineTop - shelfH;
            const float rightX = X + W - sideW;
            const float leftAreaW = W - sideW;
            r.left = rect(X, contentTop, L.leftToolbarW, bottomTop - contentTop);
            r.viewport = rect(X + L.leftToolbarW, contentTop, leftAreaW - L.leftToolbarW, bottomTop - contentTop);
            stacked(rightX, contentTop, sideW, statusTop - contentTop);
            shelf(X, bottomTop, leftAreaW);
            r.timeline = rect(X, timelineTop, leftAreaW, timelineH);
            break;
        }
        case Arrangement::Maya: {
            // Caja de herramientas + Outliner a la izquierda, Channel Box a la
            // derecha, Time Slider a todo lo ancho abajo.
            const float timelineTop = statusTop - timelineH;
            const float colH = timelineTop - contentTop;
            const float midX = X + L.leftToolbarW + L.outlinerW;
            const float rightX = X + W - sideW;
            const float midW = rightX - midX;
            const float bottomTop = timelineTop - shelfH;
            r.left = rect(X, contentTop, L.leftToolbarW, colH);
            r.objects = rect(X + L.leftToolbarW, contentTop, L.outlinerW, colH);
            r.attributes = rect(rightX, contentTop, sideW - tabsW, colH);
            if (tabsW > 0.0f) r.attrTabs = rect(rightX + sideW - tabsW, contentTop, tabsW, colH);
            r.viewport = rect(midX, contentTop, midW, bottomTop - contentTop);
            shelf(midX, bottomTop, midW);
            r.timeline = rect(X, timelineTop, W, timelineH);
            break;
        }
        case Arrangement::Max: {
            // Scene Explorer a la izquierda y Command Panel a la derecha a toda
            // altura; en medio visor, Time Slider y la fila de abajo.
            const float colH = statusTop - contentTop;
            const float midX = X + L.leftToolbarW + L.outlinerW;
            const float rightX = X + W - sideW;
            const float midW = rightX - midX;
            const float bottomTop = statusTop - shelfH;
            const float timelineTop = bottomTop - timelineH;
            r.left = rect(X, contentTop, L.leftToolbarW, colH);
            r.objects = rect(X + L.leftToolbarW, contentTop, L.outlinerW, colH);
            r.attributes = rect(rightX, contentTop, sideW - tabsW, colH);
            if (tabsW > 0.0f) r.attrTabs = rect(rightX + sideW - tabsW, contentTop, tabsW, colH);
            r.viewport = rect(midX, contentTop, midW, timelineTop - contentTop);
            r.timeline = rect(midX, timelineTop, midW, timelineH);
            shelf(midX, bottomTop, midW);
            break;
        }
        case Arrangement::SFM: {
            // Animation Set Editor + Element Viewer a la izquierda; Primary
            // Viewport arriba, Timeline grande debajo y la fila de abajo.
            const float colX = X + L.leftToolbarW;
            const float midX = colX + sideW;
            const float midW = X + W - midX;
            const float bottomTop = statusTop - shelfH;
            const float timelineTop = bottomTop - timelineH;
            r.left = rect(X, contentTop, L.leftToolbarW, statusTop - contentTop);
            stacked(colX, contentTop, sideW, statusTop - contentTop);
            r.viewport = rect(midX, contentTop, midW, timelineTop - contentTop);
            r.timeline = rect(midX, timelineTop, midW, timelineH);
            shelf(midX, bottomTop, midW);
            break;
        }
    }
    return r;
}

} // namespace iron3d
