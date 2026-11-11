#include "PresetTextures.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>

namespace iron3d {

namespace {

std::string lower(std::string t) {
    for (char& c : t) c = (char)std::tolower((unsigned char)c);
    return t;
}

std::string utf8Of(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::filesystem::path fromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::string leafOf(const std::string& p) {
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// Carpeta de la cache para una libreria: nombre legible + hash de la ruta
// completa (dos "lights.lib4d" en carpetas distintas no comparten carpeta).
std::string libraryKey(const PresetLibrary& lib) {
    std::string stem = utf8Of(lib.file().stem());
    for (char& c : stem)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    if (stem.size() > 40) stem.resize(40);
    char hash[24];
    std::snprintf(hash, sizeof(hash), "_%016llx",
                  (unsigned long long)std::hash<std::string>{}(lower(utf8Of(lib.file().lexically_normal()))));
    return stem + hash;
}

struct Index {
    const PresetLibrary* lib = nullptr;
    std::map<std::string, const PresetItem*> byLeaf; // nombre de archivo en minusculas
};

Index buildIndex(const PresetLibrary& lib) {
    Index ix;
    ix.lib = &lib;
    for (const PresetItem& it : lib.items())
        if (it.kind == PresetKind::File) ix.byLeaf.emplace(lower(leafOf(it.path)), &it);
    return ix;
}

const PresetItem* lookup(const Index& ix, const std::string& rel) {
    if (!rel.empty()) {
        const int i = ix.lib->find(rel);
        if (i >= 0 && ix.lib->items()[(size_t)i].kind == PresetKind::File) return &ix.lib->items()[(size_t)i];
    }
    auto it = ix.byLeaf.find(lower(leafOf(rel)));
    return it == ix.byLeaf.end() ? nullptr : it->second;
}

} // namespace

PresetTextureReport extractPresetTextures(const PresetLibrary& lib, const std::vector<const PresetLibrary*>& others,
                                          const std::filesystem::path& cacheDir, std::vector<Material>& materials,
                                          size_t firstNew) {
    PresetTextureReport rep;
    std::vector<Index> indices;
    indices.push_back(buildIndex(lib));
    for (const PresetLibrary* o : others)
        if (o && o != &lib) indices.push_back(buildIndex(*o));

    std::error_code ec;
    // Ruta del archivo extraido para una ruta de textura ("" = no esta).
    std::map<std::string, std::string> done;
    auto resolve = [&](const std::string& raw) -> std::string {
        auto cached = done.find(raw);
        if (cached != done.end()) return cached->second;
        std::string norm = raw;
        std::replace(norm.begin(), norm.end(), (char)92, '/');
        while (norm.size() > 2 && norm[0] == '.' && norm[1] == '/') norm.erase(0, 2);
        std::string libPart, rel = norm;
        if (lower(norm).rfind("preset://", 0) == 0) {
            const std::string rest = norm.substr(9);
            const size_t slash = rest.find('/');
            libPart = lower(slash == std::string::npos ? rest : rest.substr(0, slash));
            rel = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
        }
        // Primero la libreria que nombra la url, luego la del modelo, luego el resto.
        std::vector<const Index*> order;
        if (!libPart.empty())
            for (const Index& ix : indices)
                if (lower(ix.lib->rootUrl()) == "preset://" + libPart || lower(utf8Of(ix.lib->file().filename())) == libPart)
                    order.push_back(&ix);
        for (const Index& ix : indices)
            if (std::find(order.begin(), order.end(), &ix) == order.end()) order.push_back(&ix);
        std::string out;
        for (const Index* ix : order) {
            const PresetItem* item = lookup(*ix, rel);
            if (!item) continue;
            std::string itemPath = item->path;
            std::replace(itemPath.begin(), itemPath.end(), (char)92, '/');
            const std::filesystem::path file = cacheDir / libraryKey(*ix->lib) / fromUtf8(itemPath);
            if (!std::filesystem::is_regular_file(file, ec)) {
                std::vector<uint8_t> bytes;
                if (!ix->lib->readData(*item, bytes)) continue;
                std::filesystem::create_directories(file.parent_path(), ec);
                std::ofstream f(file, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!f) continue;
            }
            out = utf8Of(file.lexically_normal());
            break;
        }
        done.emplace(raw, out);
        return out;
    };

    for (size_t i = firstNew; i < materials.size(); ++i) {
        Material& m = materials[i];
        auto fix = [&](std::string& t) {
            if (t.empty()) return;
            if (lower(t).rfind("preset://", 0) != 0 && !resolveTexturePath(t, m.texBaseDir).empty()) {
                ++rep.alreadyOnDisk;
                return;
            }
            const std::string found = resolve(t);
            if (found.empty()) {
                if (std::find(rep.missing.begin(), rep.missing.end(), t) == rep.missing.end()) rep.missing.push_back(t);
                return;
            }
            ++rep.extracted;
            t = found; // ruta absoluta al archivo extraido
        };
        for (std::string& t : m.textures) fix(t);
        for (ReflectLayer& l : m.layers) {
            fix(l.roughnessTex);
            fix(l.colorTex);
        }
        // Las imagenes de dentro de los arboles de shaders (Filtro, Capa...):
        // mismo resultado que la textura del canal (la cache `done` lo
        // garantiza), sin contarlas otra vez.
        for (ChannelSettings& c : m.channels)
            c.shader.forEachPath([&](std::string& t) {
                if (t.empty() || (lower(t).rfind("preset://", 0) != 0 && !resolveTexturePath(t, m.texBaseDir).empty()))
                    return;
                const std::string found = resolve(t);
                if (!found.empty()) t = found;
            });
    }
    return rep;
}

} // namespace iron3d
