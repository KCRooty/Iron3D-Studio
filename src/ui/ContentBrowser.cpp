#include "ContentBrowser.h"

#include "C4DIcons.h"
#include "core/Paths.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace iron3d {

using icons::Icon;

namespace {

// --- Librerias montadas ----------------------------------------------------------

struct Mounted {
    int serial = 0; // id estable (miniaturas, arrastre, recientes)
    PresetLibrary lib;
    bool temporary = true;
};

std::vector<std::unique_ptr<Mounted>> g_libs;
int g_nextSerial = 1;

Mounted* findLib(int serial) {
    for (auto& m : g_libs)
        if (m->serial == serial) return m.get();
    return nullptr;
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) return true;
    return lowerAscii(pathUtf8(a)) == lowerAscii(pathUtf8(b));
}

// --- Estado del navegador ------------------------------------------------------------

enum class Node { Catalogs, Recent, Favorites, Computer, Presets, Network, Search, Library };

struct Location {
    Node node = Node::Presets;
    int lib = 0;        // serial (Node::Library)
    std::string folder; // ruta dentro de la libreria ("" = raiz)
    bool operator==(const Location& o) const { return node == o.node && lib == o.lib && folder == o.folder; }
};

struct RecentItem {
    int lib = 0;
    std::string path;
};

struct State {
    Location loc;
    std::vector<Location> back, fwd;
    int selLib = 0;   // seleccion en la rejilla: libreria + indice de elemento
    int selItem = -2; // -2 nada, -1 la libreria entera
    char search[128] = {};
    std::string searchDone; // texto de la ultima busqueda hecha
    float treeFrac = 0.36f;
    int iconSize = 1; // 0 pequenos, 1 medianos, 2 grandes
    bool showFiles = true;
    std::string message; // aviso en la barra inferior (clic para quitarlo)
    std::vector<RecentItem> recent;
};

State s;

void navigate(const Location& to) {
    if (to == s.loc) return;
    s.back.push_back(s.loc);
    s.fwd.clear();
    s.loc = to;
    s.selItem = -2;
}

Location libraryLocation(int serial, const std::string& folder = {}) {
    Location l;
    l.node = Node::Library;
    l.lib = serial;
    l.folder = folder;
    return l;
}

Location rootLocation(Node n) {
    Location l;
    l.node = n;
    return l;
}

// --- Miniaturas ---------------------------------------------------------------------

UiTextureCreate g_createTex;
UiTextureDestroy g_destroyTex;

struct Thumb {
    void* tex = nullptr;
    int w = 0, h = 0;
    int lastUsed = 0;
    bool failed = false;
};

std::map<std::pair<int, int>, Thumb> g_thumbs; // (serial, indice de elemento)
int g_uploadsThisFrame = 0;
int g_uploadFrame = -1;
constexpr int kMaxUploadsPerFrame = 6; // las demas llegan en los frames siguientes
constexpr size_t kMaxThumbs = 320;     // mas alla, se liberan las menos usadas

void dropThumb(Thumb& t) {
    if (t.tex && g_destroyTex) g_destroyTex(t.tex);
    t.tex = nullptr;
}

void dropThumbsOf(int serial) {
    for (auto it = g_thumbs.begin(); it != g_thumbs.end();) {
        if (it->first.first == serial) {
            dropThumb(it->second);
            it = g_thumbs.erase(it);
        } else {
            ++it;
        }
    }
}

void trimThumbs() {
    if (g_thumbs.size() <= kMaxThumbs) return;
    std::vector<std::pair<int, std::pair<int, int>>> byAge;
    for (auto& [k, t] : g_thumbs) byAge.push_back({t.lastUsed, k});
    std::sort(byAge.begin(), byAge.end());
    const int now = ImGui::GetFrameCount();
    for (const auto& [used, key] : byAge) {
        if (g_thumbs.size() <= kMaxThumbs * 3 / 4 || used >= now - 1) break;
        dropThumb(g_thumbs[key]);
        g_thumbs.erase(key);
    }
}

const Thumb* thumbFor(const Mounted& m, int index) {
    const PresetItem& it = m.lib.items()[index];
    if (!it.hasThumb() || !g_createTex) return nullptr;
    Thumb& t = g_thumbs[{m.serial, index}];
    t.lastUsed = ImGui::GetFrameCount();
    if (t.tex || t.failed) return t.tex ? &t : nullptr;
    if (g_uploadFrame != ImGui::GetFrameCount()) {
        g_uploadFrame = ImGui::GetFrameCount();
        g_uploadsThisFrame = 0;
    }
    if (g_uploadsThisFrame >= kMaxUploadsPerFrame) return nullptr;
    ++g_uploadsThisFrame;
    std::vector<uint8_t> rgba;
    if (!m.lib.readThumbnail(it, rgba, t.w, t.h) || !(t.tex = g_createTex(rgba.data(), t.w, t.h))) {
        t.failed = true;
        return nullptr;
    }
    return &t;
}

// --- Contenido de la ubicacion actual --------------------------------------------------

struct Entry {
    Mounted* lib = nullptr;
    int item = -1; // -1 = la libreria entera (en "Preestablecidos")
};

std::vector<Entry> currentEntries() {
    std::vector<Entry> out;
    switch (s.loc.node) {
        case Node::Presets:
            for (auto& m : g_libs) out.push_back({m.get(), -1});
            break;
        case Node::Library:
            if (Mounted* m = findLib(s.loc.lib)) {
                for (int i : m->lib.children(s.loc.folder)) {
                    if (!s.showFiles && m->lib.items()[i].kind == PresetKind::File) continue;
                    out.push_back({m, i});
                }
            }
            break;
        case Node::Recent:
            for (auto r = s.recent.rbegin(); r != s.recent.rend(); ++r) {
                Mounted* m = findLib(r->lib);
                if (!m) continue;
                const auto& items = m->lib.items();
                for (int i = 0; i < (int)items.size(); ++i)
                    if (items[i].path == r->path) out.push_back({m, i});
            }
            break;
        case Node::Search: {
            const std::string q = lowerAscii(s.searchDone);
            if (q.empty()) break;
            for (auto& m : g_libs) {
                const auto& items = m->lib.items();
                for (int i = 0; i < (int)items.size(); ++i) {
                    if (items[i].kind == PresetKind::Folder) continue;
                    if (!s.showFiles && items[i].kind == PresetKind::File) continue;
                    if (lowerAscii(items[i].name).find(q) != std::string::npos) out.push_back({m.get(), i});
                }
            }
            break;
        }
        default:
            break;
    }
    return out;
}

std::string locationLabel() {
    switch (s.loc.node) {
        case Node::Presets: return "Preestablecidos";
        case Node::Recent: return "Elementos Recientes";
        case Node::Search: return "Resultado de la Búsqueda: \"" + s.searchDone + "\"";
        case Node::Library:
            if (Mounted* m = findLib(s.loc.lib)) {
                std::string l = "Preestablecidos > " + m->lib.title();
                if (!s.loc.folder.empty()) {
                    std::string f = s.loc.folder;
                    for (size_t p = 0; (p = f.find('/', p)) != std::string::npos; p += 3) f.replace(p, 1, " > ");
                    l += " > " + f;
                }
                return l;
            }
            return "Preestablecidos";
        default: return "";
    }
}

// Texto recortado con "..." para que quepa en `width` (sin partir caracteres UTF-8).
std::string fitText(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string t = text;
    while (!t.empty()) {
        // Quita un caracter entero: los bytes de continuacion (10xxxxxx) y su byte inicial.
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
        if (ImGui::CalcTextSize((t + "...").c_str()).x <= width) return t + "...";
    }
    return "...";
}

Icon iconFor(const Entry& e) {
    if (e.item < 0) return Icon::CbLibrary;
    switch (e.lib->lib.items()[e.item].kind) {
        case PresetKind::Folder: return Icon::CbFolder;
        case PresetKind::Model: return Icon::CbModel;
        case PresetKind::File: break;
    }
    return Icon::CbFile;
}

// --- Acciones -------------------------------------------------------------------------

void unmount(int serial) {
    dropThumbsOf(serial);
    std::erase_if(s.recent, [&](const RecentItem& r) { return r.lib == serial; });
    std::erase_if(g_libs, [&](const auto& m) { return m->serial == serial; });
    auto stale = [&](const Location& l) { return l.node == Node::Library && l.lib == serial; };
    std::erase_if(s.back, stale);
    std::erase_if(s.fwd, stale);
    if (stale(s.loc)) s.loc = Location{};
    if (s.selLib == serial) s.selItem = -2;
}

// Copia una libreria temporal a library/browser: desde ahora sale siempre.
void makePermanent(Mounted& m) {
    const auto dir = permanentLibraryDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto dst = dir / m.lib.file().filename();
    if (std::filesystem::exists(dst, ec)) {
        s.message = "Ya hay una librería con ese nombre en library/browser.";
        return;
    }
    if (!std::filesystem::copy_file(m.lib.file(), dst, ec) || ec) {
        s.message = "No se pudo copiar la librería a library/browser: " + ec.message();
        return;
    }
    std::string err;
    PresetLibrary copy;
    if (!copy.open(dst, &err)) {
        s.message = err;
        return;
    }
    dropThumbsOf(m.serial);
    m.lib = std::move(copy);
    m.temporary = false;
    s.message = "\"" + m.lib.title() + "\" es ahora permanente (" + pathUtf8(dst) + ").";
}

void openEntry(const Entry& e, const ContentBrowserCallbacks& cb) {
    if (e.item < 0) {
        navigate(libraryLocation(e.lib->serial));
        return;
    }
    const PresetItem& it = e.lib->lib.items()[e.item];
    if (it.kind == PresetKind::Folder) navigate(libraryLocation(e.lib->serial, it.path));
    else if (it.kind == PresetKind::Model && cb.load) cb.load(e.lib->lib, it);
}

void goUp() {
    if (s.loc.node != Node::Library || s.loc.folder.empty()) {
        navigate(Location{});
        return;
    }
    const size_t p = s.loc.folder.rfind('/');
    navigate(libraryLocation(s.loc.lib, p == std::string::npos ? std::string() : s.loc.folder.substr(0, p)));
}

void goBack() {
    if (s.back.empty()) return;
    s.fwd.push_back(s.loc);
    s.loc = s.back.back();
    s.back.pop_back();
    s.selItem = -2;
}

void goForward() {
    if (s.fwd.empty()) return;
    s.back.push_back(s.loc);
    s.loc = s.fwd.back();
    s.fwd.pop_back();
    s.selItem = -2;
}

void runSearch() {
    s.searchDone = s.search;
    if (s.searchDone.empty()) return;
    if (s.loc.node == Node::Search) {
        s.selItem = -2; // buscar otra vez no apila historial
        return;
    }
    navigate(rootLocation(Node::Search));
}

const PresetItem* selectedModel() {
    if (s.selItem < 0) return nullptr;
    Mounted* m = findLib(s.selLib);
    if (!m || s.selItem >= (int)m->lib.items().size()) return nullptr;
    const PresetItem& it = m->lib.items()[s.selItem];
    return it.kind == PresetKind::Model ? &it : nullptr;
}

// --- Dibujo ---------------------------------------------------------------------------

constexpr const char* kTodo = "Todavía no implementado";

void notYet(const char* label, const char* shortcut = nullptr) {
    ImGui::MenuItem(label, shortcut, false, false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", kTodo);
}

void drawMenuBar(const ContentBrowserCallbacks& cb) {
    if (!ImGui::BeginMenuBar()) return;
    Mounted* curLib = s.loc.node == Node::Library ? findLib(s.loc.lib) : nullptr;
    if (!curLib && s.selItem == -1) curLib = findLib(s.selLib);
    int unmountSerial = 0;
    if (ImGui::BeginMenu("Archivo")) {
        notYet("Nuevo Navegador", "Ctrl+N");
        notYet("Nueva Carpeta");
        notYet("Nuevo Catálogo...");
        notYet("Nueva Librería de Preestablecidos...");
        ImGui::Separator();
        if (ImGui::MenuItem("Abrir Librería de Preestablecidos...", nullptr, false, (bool)cb.openLibrary))
            cb.openLibrary();
        if (ImGui::MenuItem("Hacer Permanente (copiar a library/browser)", nullptr, false,
                            curLib && curLib->temporary))
            makePermanent(*curLib);
        if (ImGui::MenuItem("Quitar Librería del Navegador", nullptr, false, curLib && curLib->temporary))
            unmountSerial = curLib->serial;
        ImGui::Separator();
        if (ImGui::MenuItem("Cerrar", nullptr, false, (bool)cb.close)) cb.close();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edición")) {
        const PresetItem* model = selectedModel();
        if (ImGui::MenuItem("Cargar en la Escena", "Intro", false, model && cb.load))
            cb.load(findLib(s.selLib)->lib, *model);
        ImGui::Separator();
        notYet("Cortar", "Ctrl+X");
        notYet("Copiar", "Ctrl+C");
        notYet("Pegar", "Ctrl+V");
        notYet("Borrar", "Supr");
        notYet("Renombrar...");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ver")) {
        if (ImGui::MenuItem("Iconos Pequeños", nullptr, s.iconSize == 0)) s.iconSize = 0;
        if (ImGui::MenuItem("Iconos Medianos", nullptr, s.iconSize == 1)) s.iconSize = 1;
        if (ImGui::MenuItem("Iconos Grandes", nullptr, s.iconSize == 2)) s.iconSize = 2;
        ImGui::Separator();
        ImGui::MenuItem("Mostrar Texturas y Archivos", nullptr, &s.showFiles);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ir a")) {
        if (ImGui::MenuItem("Atrás", nullptr, false, !s.back.empty())) goBack();
        if (ImGui::MenuItem("Adelante", nullptr, false, !s.fwd.empty())) goForward();
        if (ImGui::MenuItem("Subir", nullptr, false, s.loc.node != Node::Presets)) goUp();
        ImGui::Separator();
        if (ImGui::MenuItem("Preestablecidos")) navigate(Location{});
        if (ImGui::MenuItem("Elementos Recientes")) navigate(rootLocation(Node::Recent));
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
    if (unmountSerial) unmount(unmountSerial);
}

bool smallIconButton(const char* id, Icon icon, bool enabled, const char* tip) {
    const float sz = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov && enabled) dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 2);
    icons::draw(dl, icon, ImVec2(p.x + 3, p.y + 3), sz - 6, enabled);
    if (hov && tip) ImGui::SetTooltip("%s", tip);
    return pressed && enabled;
}

void drawToolbar() {
    if (smallIconButton("##cbback", Icon::Back, !s.back.empty(), "Atrás")) goBack();
    ImGui::SameLine(0, 2);
    if (smallIconButton("##cbfwd", Icon::Forward, !s.fwd.empty(), "Adelante")) goForward();
    ImGui::SameLine(0, 2);
    if (smallIconButton("##cbup", Icon::Up, s.loc.node != Node::Presets, "Subir")) goUp();
    ImGui::SameLine(0, 6);
    const float searchW = std::clamp(ImGui::GetContentRegionAvail().x * 0.38f, 90.0f, 220.0f);
    const float pathW = ImGui::GetContentRegionAvail().x - searchW - 8;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fitText(locationLabel(), pathW).c_str());
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - searchW);
    ImGui::SetNextItemWidth(searchW);
    if (ImGui::InputTextWithHint("##cbsearch", "Buscar...", s.search, sizeof(s.search),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
        runSearch();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Busca por nombre en todas las librerías (Intro)");
}

// Fila del arbol con icono propio. Devuelve si esta abierta (y hay que hacer TreePop).
bool treeRow(const void* id, Icon icon, const std::string& label, bool selected, bool leaf, bool enabled,
             bool* clicked, bool defaultOpen = false) {
    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                           ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_FramePadding;
    if (selected) f |= ImGuiTreeNodeFlags_Selected;
    if (leaf) f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (defaultOpen) f |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 1));
    const bool open = ImGui::TreeNodeEx(id, f, "%s", "");
    ImGui::PopStyleVar();
    const bool hov = ImGui::IsItemHovered();
    if (clicked) *clicked = enabled && ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const float x = a.x + ImGui::GetTreeNodeToLabelSpacing();
    const float sz = b.y - a.y - 2;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    icons::draw(dl, icon, ImVec2(x, a.y + 1), sz, enabled);
    dl->AddText(ImVec2(x + sz + 4, a.y + (b.y - a.y - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                fitText(label, std::max(10.0f, b.x - x - sz - 6)).c_str());
    if (hov && !enabled) ImGui::SetTooltip("%s", kTodo);
    return open && !leaf;
}

bool hasSubfolders(const Mounted& m, const std::string& folder) {
    for (int c : m.lib.children(folder))
        if (m.lib.items()[c].kind == PresetKind::Folder) return true;
    return false;
}

void drawFolderTree(Mounted& m, const std::string& folder) {
    for (int i : m.lib.children(folder)) {
        const PresetItem& it = m.lib.items()[i];
        if (it.kind != PresetKind::Folder) continue;
        bool clicked = false;
        const bool sel = s.loc.node == Node::Library && s.loc.lib == m.serial && s.loc.folder == it.path;
        const bool open = treeRow(&it, Icon::CbFolder, it.name, sel, !hasSubfolders(m, it.path), true, &clicked);
        if (clicked) navigate(libraryLocation(m.serial, it.path));
        if (open) {
            drawFolderTree(m, it.path);
            ImGui::TreePop();
        }
    }
}

// Menu contextual de una libreria. Devuelve el serial a desmontar (0 = ninguno):
// se desmonta fuera, cuando ya nadie usa la referencia.
int libraryContextMenu(Mounted& m) {
    int unmountSerial = 0;
    if (!ImGui::BeginPopupContextItem("##libctx")) return 0;
    ImGui::TextDisabled("%s", m.temporary ? "Librería temporal (solo esta sesión)" : "Librería permanente");
    ImGui::Separator();
    if (ImGui::MenuItem("Hacer Permanente (copiar a library/browser)", nullptr, false, m.temporary)) makePermanent(m);
    if (ImGui::MenuItem("Quitar del Navegador", nullptr, false, m.temporary)) unmountSerial = m.serial;
    ImGui::EndPopup();
    return unmountSerial;
}

void drawTree() {
    static const char* kRoot[] = {"Catálogos", "Elementos Recientes", "Favoritos", "Ordenador",
                                  "Preestablecidos", "Red", "Resultado de la Búsqueda"};
    static const Icon kIcon[] = {Icon::CbCatalog, Icon::CbRecent,  Icon::CbFavorites,    Icon::CbComputer,
                                 Icon::CbPresets, Icon::CbNetwork, Icon::CbSearchResults};
    static const Node kNode[] = {Node::Catalogs, Node::Recent,  Node::Favorites, Node::Computer,
                                 Node::Presets,  Node::Network, Node::Search};
    int unmountSerial = 0;
    for (int r = 0; r < 7; ++r) {
        const bool enabled = kNode[r] == Node::Recent || kNode[r] == Node::Presets || kNode[r] == Node::Search;
        const bool isPresets = kNode[r] == Node::Presets;
        bool clicked = false;
        const bool open = treeRow(kRoot[r], kIcon[r], kRoot[r], s.loc.node == kNode[r], !isPresets, enabled,
                                  &clicked, isPresets);
        if (clicked) navigate(rootLocation(kNode[r]));
        if (!open) continue;
        for (auto& mp : g_libs) {
            Mounted& m = *mp;
            const bool sel = s.loc.node == Node::Library && s.loc.lib == m.serial && s.loc.folder.empty();
            bool libClicked = false;
            const std::string label = m.temporary ? m.lib.title() + "  (temporal)" : m.lib.title();
            ImGui::PushID(m.serial);
            const bool libOpen = treeRow(&m, Icon::CbLibrary, label, sel, !hasSubfolders(m, ""), true, &libClicked);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s", pathUtf8(m.lib.file()).c_str(),
                                  m.temporary ? "Temporal: solo en esta sesión" : "Permanente (library/browser)");
            if (int u = libraryContextMenu(m)) unmountSerial = u;
            ImGui::PopID();
            if (libClicked) navigate(libraryLocation(m.serial));
            if (libOpen) {
                drawFolderTree(m, "");
                ImGui::TreePop();
            }
        }
        if (g_libs.empty()) ImGui::TextDisabled("   (ninguna librería)");
        ImGui::TreePop();
    }
    if (unmountSerial) unmount(unmountSerial);
}

void drawGrid(const ContentBrowserCallbacks& cb) {
    const std::vector<Entry> entries = currentEntries();
    const float thumb = s.iconSize == 0 ? 56.0f : s.iconSize == 1 ? 84.0f : 120.0f;
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 cell(thumb + 12, thumb + lineH + 12);
    const float availW = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)(availW / cell.x));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 selCol = ImGui::GetColorU32(ImGuiCol_Header);
    const ImU32 hovCol = ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f);
    const ImU32 frameCol = ImGui::GetColorU32(ImGuiCol_FrameBg);

    if (entries.empty()) {
        const char* msg = s.loc.node == Node::Presets
                              ? "No hay librerías. Abre un .lib4d (Archivo > Abrir) o cópialo a library/browser."
                          : s.loc.node == Node::Recent ? "Aún no has cargado ningún preestablecido en esta sesión."
                          : s.loc.node == Node::Search ? "Sin resultados."
                                                        : "Carpeta vacía.";
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", msg);
        ImGui::PopTextWrapPos();
        return;
    }

    int unmountSerial = 0;
    Entry pendingOpen;
    ImGuiListClipper clipper;
    const int rows = ((int)entries.size() + cols - 1) / cols;
    clipper.Begin(rows, cell.y);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int c = 0; c < cols; ++c) {
                const int idx = row * cols + c;
                if (idx >= (int)entries.size()) break;
                const Entry& e = entries[idx];
                if (c > 0) ImGui::SameLine(0, 0);
                ImGui::PushID(idx);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##cell", cell);
                const bool hov = ImGui::IsItemHovered();
                const bool sel = s.selLib == e.lib->serial && s.selItem == e.item;
                if (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    s.selLib = e.lib->serial;
                    s.selItem = e.item;
                }
                const bool dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                const PresetItem* it = e.item >= 0 ? &e.lib->lib.items()[e.item] : nullptr;
                // Arrastrar un modelo al visor lo fusiona con la escena.
                if (it && it->kind == PresetKind::Model && ImGui::BeginDragDropSource()) {
                    const int payload[2] = {e.lib->serial, e.item};
                    ImGui::SetDragDropPayload(kPresetPayloadType, payload, sizeof(payload));
                    ImGui::Text("%s", it->name.c_str());
                    ImGui::TextDisabled("Suéltalo en el visor para añadirlo a la escena");
                    ImGui::EndDragDropSource();
                }
                if (it) {
                    if (ImGui::BeginPopupContextItem("##cellctx")) {
                        if (ImGui::MenuItem("Cargar en la Escena", nullptr, false,
                                            it->kind == PresetKind::Model && cb.load))
                            cb.load(e.lib->lib, *it);
                        if (ImGui::MenuItem("Abrir", nullptr, false, it->kind == PresetKind::Folder))
                            openEntry(e, cb);
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", it->path.c_str());
                        ImGui::EndPopup();
                    }
                } else if (int u = libraryContextMenu(*e.lib)) {
                    unmountSerial = u;
                }
                if (hov && it) {
                    const char* kind = it->kind == PresetKind::Model    ? "Modelo / escena de Cinema 4D"
                                       : it->kind == PresetKind::Folder ? "Carpeta"
                                                                        : "Archivo (textura, imagen...)";
                    const char* how = it->kind == PresetKind::Model    ? "\nDoble clic o arrastrar al visor: cargar"
                                      : it->kind == PresetKind::Folder ? "\nDoble clic: abrir"
                                                                       : "";
                    ImGui::SetTooltip("%s\n%s%s", it->name.c_str(), kind, how);
                } else if (hov) {
                    ImGui::SetTooltip("%s\n%d modelos  ·  %s", e.lib->lib.title().c_str(), e.lib->lib.modelCount(),
                                      e.lib->temporary ? "temporal (solo esta sesión)" : "permanente");
                }

                const ImVec2 b(p.x + cell.x, p.y + cell.y);
                if (sel) dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(b.x - 2, b.y - 2), selCol, 3);
                else if (hov) dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(b.x - 2, b.y - 2), hovCol, 3);
                const ImVec2 ta(p.x + 6, p.y + 5), tb(ta.x + thumb, ta.y + thumb);
                const Thumb* t = it ? thumbFor(*e.lib, e.item) : nullptr;
                if (t) {
                    dl->AddRectFilled(ta, tb, frameCol);
                    // Encajar respetando la proporcion.
                    const float k = std::min(thumb / (float)t->w, thumb / (float)t->h);
                    const float w = (float)t->w * k, h = (float)t->h * k;
                    const ImVec2 ia(ta.x + (thumb - w) * 0.5f, ta.y + (thumb - h) * 0.5f);
                    dl->AddImage(reinterpret_cast<ImTextureID>(t->tex), ia, ImVec2(ia.x + w, ia.y + h));
                } else {
                    const float is = thumb * 0.62f;
                    icons::draw(dl, iconFor(e), ImVec2(ta.x + (thumb - is) * 0.5f, ta.y + (thumb - is) * 0.5f), is,
                                true);
                }
                const std::string label = fitText(it ? it->name : e.lib->lib.title(), cell.x - 6);
                const float lw = ImGui::CalcTextSize(label.c_str()).x;
                dl->AddText(ImVec2(p.x + (cell.x - lw) * 0.5f, tb.y + 3), ImGui::GetColorU32(ImGuiCol_Text),
                            label.c_str());
                ImGui::PopID();
                if (dbl) pendingOpen = e; // se abre al final: puede navegar y cambiar `entries`
            }
        }
    }
    clipper.End();
    if (pendingOpen.lib) openEntry(pendingOpen, cb);

    // Intro carga el modelo seleccionado.
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && cb.load) {
        if (const PresetItem* m = selectedModel()) cb.load(findLib(s.selLib)->lib, *m);
    }
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        s.selItem = -2;
    if (unmountSerial) unmount(unmountSerial);
}

std::string statusText(const std::vector<Entry>& entries) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d elementos", (int)entries.size());
    std::string out = buf;
    if (s.loc.node == Node::Library)
        if (Mounted* m = findLib(s.loc.lib))
            out += m->temporary ? "  ·  librería temporal (solo esta sesión)" : "  ·  librería permanente";
    return out;
}

} // namespace

// --- API -------------------------------------------------------------------------------

void setUiTextureFactory(UiTextureCreate create, UiTextureDestroy destroy) {
    g_createTex = std::move(create);
    g_destroyTex = std::move(destroy);
}

void releaseContentBrowserTextures() {
    for (auto& [k, t] : g_thumbs) dropThumb(t);
    g_thumbs.clear();
}

std::filesystem::path permanentLibraryDir() { return exeDirectory() / "library" / "browser"; }

void scanPermanentLibraries() {
    const auto dir = permanentLibraryDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::vector<std::filesystem::path> files;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec))
        if (de.is_regular_file(ec) && isLib4dPath(de.path())) files.push_back(de.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        bool already = false;
        for (auto& m : g_libs) already = already || samePath(m->lib.file(), f);
        if (already) continue;
        auto m = std::make_unique<Mounted>();
        std::string err;
        if (!m->lib.open(f, &err)) {
            std::fprintf(stderr, "[Iron3D] Librería '%s' ignorada: %s\n", pathUtf8(f).c_str(), err.c_str());
            continue;
        }
        m->serial = g_nextSerial++;
        m->temporary = false;
        g_libs.push_back(std::move(m));
    }
}

bool mountTemporaryLibrary(const std::filesystem::path& file, std::string* outError) {
    for (auto& m : g_libs) {
        if (samePath(m->lib.file(), file)) {
            navigate(libraryLocation(m->serial));
            return true;
        }
    }
    auto m = std::make_unique<Mounted>();
    if (!m->lib.open(file, outError)) return false;
    m->serial = g_nextSerial++;
    m->temporary = true;
    const int serial = m->serial;
    g_libs.push_back(std::move(m));
    navigate(libraryLocation(serial));
    s.message.clear();
    return true;
}

const PresetLibrary* findMountedLibrary(const std::filesystem::path& file) {
    for (auto& m : g_libs)
        if (samePath(m->lib.file(), file)) return &m->lib;
    return nullptr;
}

void noteRecentPreset(const PresetLibrary& lib, const PresetItem& item) {
    for (auto& m : g_libs) {
        if (&m->lib != &lib) continue;
        std::erase_if(s.recent, [&](const RecentItem& r) { return r.lib == m->serial && r.path == item.path; });
        s.recent.push_back({m->serial, item.path});
        if (s.recent.size() > 40) s.recent.erase(s.recent.begin());
        return;
    }
}

bool resolvePresetPayload(const ImGuiPayload* payload, const PresetLibrary*& lib, const PresetItem*& item) {
    if (!payload || !payload->IsDataType(kPresetPayloadType) || payload->DataSize != (int)(2 * sizeof(int)))
        return false;
    const int* d = static_cast<const int*>(payload->Data);
    Mounted* m = findLib(d[0]);
    if (!m || d[1] < 0 || d[1] >= (int)m->lib.items().size()) return false;
    lib = &m->lib;
    item = &m->lib.items()[d[1]];
    return item->kind == PresetKind::Model;
}

void drawContentBrowser(ImVec2 pos, ImVec2 size, const ContentBrowserCallbacks& cb) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Navegador de Contenido###contentbrowser", nullptr, flags)) {
        ImGui::End();
        return;
    }
    drawMenuBar(cb);
    drawToolbar();

    const float statusH = ImGui::GetTextLineHeightWithSpacing() + 2;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bodyH = std::max(40.0f, avail.y - statusH);
    const float splitW = 5.0f;
    const float treeW = std::clamp(avail.x * s.treeFrac, 90.0f, std::max(90.0f, avail.x - 120.0f));

    ImGui::BeginChild("##cbtree", ImVec2(treeW, bodyH), ImGuiChildFlags_Border);
    drawTree();
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##cbsplit", ImVec2(splitW, bodyH));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && avail.x > 0)
        s.treeFrac = std::clamp(s.treeFrac + ImGui::GetIO().MouseDelta.x / avail.x, 0.15f, 0.7f);
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##cbgrid", ImVec2(0, bodyH), ImGuiChildFlags_Border);
    drawGrid(cb);
    ImGui::EndChild();

    const std::string st = s.message.empty() ? statusText(currentEntries()) : s.message;
    ImGui::TextDisabled("%s", fitText(st, ImGui::GetContentRegionAvail().x).c_str());
    if (!s.message.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n(clic para quitar el aviso)", s.message.c_str());
    if (!s.message.empty() && ImGui::IsItemClicked()) s.message.clear();
    trimThumbs();
    ImGui::End();
}

} // namespace iron3d
