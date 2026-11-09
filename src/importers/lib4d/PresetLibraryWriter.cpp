#include "importers/lib4d/PresetLibraryWriter.h"

#include "importers/lib4d/PresetLibrary.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Compresor zlib de stb_image_write (implementado en StbImpl.cpp). La cabecera
// de stb no lo declara fuera de la implementacion.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace iron3d {

namespace {

// --- Bytes ----------------------------------------------------------------------

using Bytes = std::vector<uint8_t>;

void putU32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}
void putU64(Bytes& b, uint64_t v) {
    for (int s = 56; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

// UTF-8 -> UTF-16BE (con pares sustitutos). Bytes invalidos -> U+FFFD.
Bytes utf16be(const std::string& s) {
    Bytes out;
    auto unit = [&](uint32_t u) {
        out.push_back(static_cast<uint8_t>(u >> 8));
        out.push_back(static_cast<uint8_t>(u));
    };
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        // Longitud de la secuencia segun el primer byte; solo vale si todos los
        // bytes de continuacion son 10xxxxxx. Si no, ese byte sale como U+FFFD
        // y se sigue en el siguiente (nunca se comen bytes ajenos).
        size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<uint8_t>(s[i + k]) & 0xC0) == 0x80;
        uint32_t cp = 0xFFFD;
        if (!ok) {
            n = 1;
        } else if (n == 1) {
            cp = c;
        } else {
            cp = c & (0x7Fu >> n);
            for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3Fu);
        }
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unit(0xD800 + (cp >> 10));
            unit(0xDC00 + (cp & 0x3FF));
        } else {
            unit(cp);
        }
    }
    return out;
}

// Minusculas ASCII + Latin-1 (lo que se ve en las urls reales); el resto igual.
std::string lowerUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c + 32));
        } else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0x80 && d <= 0x9E && d != 0x97) d = static_cast<unsigned char>(d + 0x20); // A..Y con tilde
            out.push_back(static_cast<char>(c));
            out.push_back(static_cast<char>(d));
            ++i;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

uint32_t crc32(const Bytes& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// --- Contenedores serializados (los registros "QC4Dbrow") -----------------------
// Valor entero = 0x0f + i32; entero de 64 bits = 0x11 + i64; cadena = 0x82 +
// u32 bytes + UTF-16BE; contenedor = 0x01 + u32 132 + u32 1 + id, elementos
// (id, tipo, valor) y 0x02. Tipos de elemento: 15 entero, 26 entero de 64
// bits, 130 cadena, 132 contenedor.

constexpr uint32_t kR19Version = 19024;
// Codigos de idioma de los nombres visibles en R19 ("es", "us"): se escriben
// los dos para que el nombre salga en C4D en espanol y en ingles.
constexpr int32_t kLangs[] = {0x65730000, 0x75730000};
constexpr int32_t kTextureType = 1018031;
constexpr int32_t kTextureSubType = 1018021;

struct Rec {
    Bytes b;
    void i32v(int32_t v) {
        b.push_back(0x0F);
        putU32(b, static_cast<uint32_t>(v));
    }
    void begin(int32_t cid) {
        b.push_back(0x01);
        putU32(b, 132);
        putU32(b, 1);
        i32v(cid);
    }
    void end() { b.push_back(0x02); }
    void elInt(int32_t key, int32_t v) {
        i32v(key);
        i32v(15);
        i32v(v);
    }
    void elInt64(int32_t key, uint64_t v) {
        i32v(key);
        i32v(26);
        b.push_back(0x11);
        putU64(b, v);
    }
    void elStr(int32_t key, const std::string& s) {
        i32v(key);
        i32v(130);
        const Bytes u = utf16be(s);
        b.push_back(0x82);
        putU32(b, static_cast<uint32_t>(u.size()));
        b.insert(b.end(), u.begin(), u.end());
    }
    void elBegin(int32_t key, int32_t cid = -1) {
        i32v(key);
        i32v(132);
        begin(cid);
    }
};

Bytes recordHeader() {
    Bytes b = {'Q', 'C', '4', 'D', 'b', 'r', 'o', 'w'};
    putU32(b, kR19Version);
    b.push_back(0);
    return b;
}

// Url + hash (clave 6) y nombre visible (clave 16).
void identity(Rec& r, const std::string& url, const std::string* name) {
    r.elBegin(6);
    r.elStr(0, url);
    r.elInt(2, lib4d::urlHash(url));
    r.end();
    if (name) {
        r.elBegin(16);
        for (int32_t l : kLangs) r.elStr(l, *name);
        r.end();
    }
}

// Fecha y hora (claves 11 y 13: creado y modificado). Fecha = anio<<16 |
// mes<<8 | dia (seguro). Hora = h<<27 | min<<21 | s<<15 (cuadra con los
// ejemplos reales; solo afecta a lo que se muestra).
void stamp(Rec& r, int32_t key) {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    const uint32_t date = (static_cast<uint32_t>(t.tm_year + 1900) << 16) | (static_cast<uint32_t>(t.tm_mon + 1) << 8) |
                          static_cast<uint32_t>(t.tm_mday);
    const uint32_t time = (static_cast<uint32_t>(t.tm_hour) << 27) | (static_cast<uint32_t>(t.tm_min) << 21) |
                          (static_cast<uint32_t>(t.tm_sec) << 15);
    r.elBegin(key);
    r.elInt(0, static_cast<int32_t>(date));
    r.elInt(1, static_cast<int32_t>(time));
    r.end();
}

Bytes finish(Rec& r) {
    Bytes out = recordHeader();
    out.insert(out.end(), r.b.begin(), r.b.end());
    return out;
}

Bytes folderRecord(uint32_t id, const std::string& url, const std::string& name) {
    Rec r;
    r.begin(static_cast<int32_t>(id));
    identity(r, url, &name);
    r.elBegin(10);
    r.elInt(0, 8);
    r.end();
    r.end();
    return finish(r);
}

Bytes rootRecord(const std::string& url) {
    Rec r;
    r.begin(1);
    identity(r, url, nullptr);
    r.elBegin(10);
    r.elBegin(6);
    for (int32_t k = 4; k <= 7; ++k) r.elInt(k, 0);
    r.end();
    r.elInt(0, 8);
    r.end();
    r.end();
    return finish(r);
}

Bytes headerRecord(const std::string& url, const std::string& title) {
    Rec r;
    r.begin(0);
    r.elInt(0, 7);
    r.elStr(3, url);
    r.elInt(5, 0);
    r.elBegin(4);
    for (int32_t l : kLangs) r.elStr(l, title);
    r.end();
    r.end();
    return finish(r);
}

void putChunk(Bytes& out, const char tag[4], uint32_t id, const Bytes& body) {
    out.insert(out.end(), tag, tag + 4);
    putU32(out, id);
    putU64(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
}

bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

std::string parentOf(const std::string& path) {
    const size_t s = path.rfind('/');
    return s == std::string::npos ? std::string() : path.substr(0, s);
}
std::string leafOf(const std::string& path) {
    const size_t s = path.rfind('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

// Lo que hace falta saber de una libreria existente para anadirle cosas.
struct LibState {
    std::string rootUrl;
    uint32_t maxId = 1;
    uint64_t endOffset = 0; // donde empieza el "dbfe" final
};

bool scanLibrary(const std::filesystem::path& file, LibState& st, std::string* err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(err, "No se puede abrir la librería.");
    in.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(in.tellg());
    char head[4] = {};
    in.seekg(0);
    in.read(head, 4);
    if (size < 32 || std::memcmp(head, "dbfh", 4) != 0) return fail(err, "No es una librería .lib4d.");
    uint64_t off = 16;
    bool closed = false;
    while (off + 16 <= size) {
        uint8_t h[16];
        in.seekg(static_cast<std::streamoff>(off));
        in.read(reinterpret_cast<char*>(h), 16);
        if (!in) break;
        const uint32_t id = (uint32_t(h[4]) << 24) | (uint32_t(h[5]) << 16) | (uint32_t(h[6]) << 8) | h[7];
        uint64_t len = 0;
        for (int k = 8; k < 16; ++k) len = (len << 8) | h[k];
        if (std::memcmp(h, "dbfe", 4) == 0) {
            st.endOffset = off;
            closed = off + 16 == size;
            break;
        }
        if (len > size - off - 16) return fail(err, "La librería está cortada o dañada.");
        st.maxId = std::max(st.maxId, id);
        if (std::memcmp(h, "difm", 4) == 0 && len < (1u << 20)) {
            std::vector<uint8_t> body(static_cast<size_t>(len));
            in.read(reinterpret_cast<char*>(body.data()), static_cast<std::streamsize>(len));
            const std::vector<std::string> strs = lib4d::recordStrings(body);
            if (!strs.empty()) st.rootUrl = strs.front();
        }
        off += 16 + len;
    }
    if (!closed) return fail(err, "La librería no termina como se esperaba (falta el cierre \"dbfe\").");
    if (st.rootUrl.rfind("preset://", 0) != 0) return fail(err, "La librería no tiene cabecera (difm).");
    return true;
}

// --- Edicion (borrar / mover / reescribir) ---------------------------------------

struct Block {
    char tag[4];
    uint32_t id = 0;
    uint64_t offset = 0, size = 0; // offset = cabecera del bloque
};

bool scanBlocks(const std::filesystem::path& file, std::vector<Block>& out, std::string* err) {
    out.clear();
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(err, "No se puede abrir la librería.");
    in.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(in.tellg());
    char head[4] = {};
    in.seekg(0);
    in.read(head, 4);
    if (size < 32 || std::memcmp(head, "dbfh", 4) != 0) return fail(err, "No es una librería .lib4d.");
    uint64_t off = 16;
    while (off + 16 <= size) {
        uint8_t h[16];
        in.seekg(static_cast<std::streamoff>(off));
        in.read(reinterpret_cast<char*>(h), 16);
        if (!in) break;
        Block b;
        std::memcpy(b.tag, h, 4);
        b.id = (uint32_t(h[4]) << 24) | (uint32_t(h[5]) << 16) | (uint32_t(h[6]) << 8) | h[7];
        for (int k = 8; k < 16; ++k) b.size = (b.size << 8) | h[k];
        b.offset = off;
        if (b.size > size - off - 16) return fail(err, "La librería está cortada o dañada.");
        out.push_back(b);
        if (std::memcmp(h, "dbfe", 4) == 0) return true;
        off += 16 + b.size;
    }
    return fail(err, "La librería no termina como se esperaba (falta el cierre \"dbfe\").");
}

bool readBody(std::ifstream& in, const Block& b, Bytes& out) {
    out.resize(static_cast<size_t>(b.size));
    in.clear();
    in.seekg(static_cast<std::streamoff>(b.offset + 16));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<uint64_t>(in.gcount()) == b.size;
}

bool isTag(const Block& b, const char* t) { return std::memcmp(b.tag, t, 4) == 0; }

// Ultimo bloque vigente de cada id con esa etiqueta.
std::map<uint32_t, size_t> lastOf(const std::vector<Block>& blocks, const char* tag) {
    std::map<uint32_t, size_t> m;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (isTag(blocks[i], tag)) m[blocks[i].id] = i;
    return m;
}

// Anula bloques en su sitio: escribe "ivld" encima de la etiqueta.
bool invalidate(std::fstream& io, const Block& b) {
    io.seekp(static_cast<std::streamoff>(b.offset));
    io.write("ivld", 4);
    return static_cast<bool>(io);
}

std::string cleanPath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}
bool badPath(const std::string& p) {
    return p.empty() || p.front() == '/' || p.back() == '/' || p.find("//") != std::string::npos;
}
// `p` es `root` o esta dentro de `root`.
bool under(const std::string& p, const std::string& root) {
    const std::string lp = lowerUtf8(p), lr = lowerUtf8(root);
    return lp == lr || (lp.size() > lr.size() && lp.compare(0, lr.size(), lr) == 0 && lp[lr.size()] == '/');
}

void setUrl(lib4d::BcValue& root, const std::string& url) {
    lib4d::BcValue* k6 = root.find(6);
    if (!k6) return;
    if (lib4d::BcValue* u = k6->find(0)) u->setStr(url);
    if (lib4d::BcValue* h = k6->find(2)) h->i = lib4d::urlHash(url);
}
void setNames(lib4d::BcValue& root, const std::string& name) {
    if (lib4d::BcValue* n = root.find(16))
        for (auto& kv : n->kids)
            if (kv.second.type == 130) kv.second.setStr(name);
}

} // namespace

namespace lib4d {
int32_t urlHash(const std::string& urlUtf8) {
    return static_cast<int32_t>(crc32(utf16be(lowerUtf8(urlUtf8))));
}
} // namespace lib4d

bool createPresetLibrary(const std::filesystem::path& file, const std::string& title, std::string* err) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return fail(err, "Ya existe una librería con ese nombre.");
    const std::u8string fname = file.filename().u8string();
    const std::string url = "preset://" + std::string(fname.begin(), fname.end());
    Bytes out = {'d', 'b', 'f', 'h'};
    out.resize(16, 0);
    putChunk(out, "ditm", 1, rootRecord(url));
    putChunk(out, "difm", 0, headerRecord(url, title));
    putChunk(out, "dbfe", 0, {});
    std::ofstream o(file, std::ios::binary);
    if (!o) return fail(err, "No se puede crear el archivo.");
    o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!o) return fail(err, "No se pudo escribir el archivo.");
    return true;
}

bool appendToPresetLibrary(const std::filesystem::path& file, const std::vector<lib4d::NewEntry>& entries,
                           std::string* err) {
    LibState st;
    if (!scanLibrary(file, st, err)) return false;
    PresetLibrary existing;
    if (!existing.open(file, err)) return false;
    std::set<std::string> taken;
    for (const PresetItem& it : existing.items()) taken.insert(lowerUtf8(it.path));

    // Rutas limpias, sin repetidas ni ocupadas.
    std::set<std::string> fresh;
    for (const lib4d::NewEntry& e : entries) {
        std::string p = e.path;
        std::replace(p.begin(), p.end(), '\\', '/');
        if (p.empty() || p.front() == '/' || p.back() == '/' || p.find("//") != std::string::npos)
            return fail(err, "Ruta no válida: \"" + e.path + "\".");
        const std::string key = lowerUtf8(p);
        if (taken.count(key) || fresh.count(key)) return fail(err, "Ya hay un elemento \"" + p + "\" en la librería.");
        fresh.insert(key);
    }

    uint64_t off = st.endOffset;
    uint32_t nextId = st.maxId + 1;
    Bytes data, records;
    std::set<std::string> folders; // carpetas que ya existen o se crean ahora
    for (const PresetItem& it : existing.items())
        if (it.kind == PresetKind::Folder) folders.insert(lowerUtf8(it.path));

    auto ensureFolder = [&](const std::string& path, auto&& self) -> void {
        if (path.empty() || folders.count(lowerUtf8(path))) return;
        self(parentOf(path), self);
        folders.insert(lowerUtf8(path));
        putChunk(records, "ditm", nextId, folderRecord(nextId, st.rootUrl + "/" + path, leafOf(path)));
        ++nextId;
    };

    for (const lib4d::NewEntry& e : entries) {
        std::string path = e.path;
        std::replace(path.begin(), path.end(), '\\', '/');
        if (e.data.empty()) {
            ensureFolder(path, ensureFolder);
            continue;
        }
        ensureFolder(parentOf(path), ensureFolder);
        const uint32_t id = nextId++;
        const std::string url = st.rootUrl + "/" + path;
        const bool isDoc = e.data.size() >= 4 && std::memcmp(e.data.data(), "QC4D", 4) == 0;
        const uint64_t binaOffset = off + data.size();

        Rec r;
        r.begin(static_cast<int32_t>(id));
        std::string name = leafOf(path);
        // Orden de las claves como en las librerias reales. En los modelos el
        // bloque 14 (datos) va antes del nombre y el tipo (1) despues.
        identity(r, url, isDoc ? nullptr : &name);
        if (isDoc) {
            if (e.data.size() > 0x7FFFFFFF) return fail(err, "\"" + path + "\" es demasiado grande.");
            int zlen = 0;
            unsigned char* z = stbi_zlib_compress(const_cast<unsigned char*>(e.data.data()),
                                                  static_cast<int>(e.data.size()), &zlen, 8);
            if (!z) return fail(err, "No se pudo comprimir \"" + path + "\".");
            Bytes body;
            putU32(body, static_cast<uint32_t>(e.data.size()));
            body.insert(body.end(), z, z + zlen);
            std::free(z);
            putChunk(data, "bina", id, body);
            r.elBegin(14);
            r.elInt(1, 2); // 2 = zlib
            r.elInt64(2, e.data.size());
            r.elInt64(0, binaOffset);
            r.end();
            r.elBegin(16);
            for (int32_t l : kLangs) r.elStr(l, name);
            r.end();
            r.elInt(1, static_cast<int32_t>(kLib4dDocumentType));
            stamp(r, 11);
            stamp(r, 13);
            // Miniatura: bloque "imag" (JPEG) con el mismo id; la clave 5
            // apunta al principio del bloque, como en las librerias reales.
            const size_t px = static_cast<size_t>(std::max(0, e.thumbWidth)) * static_cast<size_t>(std::max(0, e.thumbHeight));
            if (px > 0 && e.thumbRgba.size() >= px * 4) {
                Bytes jpeg;
                auto sink = [](void* ctx, void* bytes, int size) {
                    auto* out = static_cast<Bytes*>(ctx);
                    out->insert(out->end(), static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + size);
                };
                if (stbi_write_jpg_to_func(sink, &jpeg, e.thumbWidth, e.thumbHeight, 4, e.thumbRgba.data(), 90) &&
                    !jpeg.empty()) {
                    const uint64_t imagOffset = off + data.size();
                    putChunk(data, "imag", id, jpeg);
                    r.elBegin(5);
                    r.elBegin(0, 0);
                    r.elInt(1, e.thumbWidth);
                    r.elInt(2, e.thumbHeight);
                    r.elInt(3, 8);
                    stamp(r, 4);
                    r.elInt64(5, imagOffset);
                    r.end();
                    r.end();
                }
            }
        } else {
            putChunk(data, "bina", id, e.data);
            stamp(r, 11);
            stamp(r, 13);
            r.elInt(1, kTextureType);
            r.elInt(17, kTextureSubType);
            r.elBegin(14);
            r.elInt(1, 0); // 0 = tal cual
            r.elInt64(2, e.data.size());
            r.elInt64(0, binaOffset);
            r.end();
            int w = 0, h = 0, comp = 0;
            if (e.data.size() < 0x7FFFFFFF &&
                stbi_info_from_memory(e.data.data(), static_cast<int>(e.data.size()), &w, &h, &comp)) {
                const bool grey = comp <= 2;
                r.elBegin(4);
                r.elInt(0, w);
                r.elInt(1, h);
                r.elInt(2, grey ? 2 : 4);
                r.elInt(3, (comp == 2 || comp == 4) ? 1 : 0);
                r.elStr(4, grey ? "Grey Gamma 2.2" : "sRGB IEC61966-2.1");
                r.end();
            }
        }
        r.end();
        putChunk(records, "ditm", id, finish(r));
    }

    data.insert(data.end(), records.begin(), records.end());
    putChunk(data, "dbfe", 0, {});

    std::fstream io(file, std::ios::in | std::ios::out | std::ios::binary);
    if (!io) return fail(err, "No se puede abrir la librería para escribir.");
    io.seekp(static_cast<std::streamoff>(st.endOffset));
    io.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!io) return fail(err, "No se pudo escribir en la librería.");
    return true;
}

} // namespace iron3d

namespace iron3d {

bool removeFromPresetLibrary(const std::filesystem::path& file, const std::vector<std::string>& paths,
                             std::string* err) {
    PresetLibrary lib;
    if (!lib.open(file, err)) return false;
    std::vector<Block> blocks;
    if (!scanBlocks(file, blocks, err)) return false;
    std::vector<std::string> roots;
    for (const std::string& raw : paths) {
        const std::string p = cleanPath(raw);
        if (badPath(p) || lib.find(p) < 0) return fail(err, "No existe \"" + raw + "\" en la librería.");
        roots.push_back(p);
    }
    std::set<uint32_t> ids;
    for (const PresetItem& it : lib.items())
        for (const std::string& r : roots)
            if (it.id > 1 && under(it.path, r)) ids.insert(it.id);
    std::fstream io(file, std::ios::in | std::ios::out | std::ios::binary);
    if (!io) return fail(err, "No se puede abrir la librería para escribir.");
    for (const Block& b : blocks)
        if (ids.count(b.id) && (isTag(b, "ditm") || isTag(b, "bina") || isTag(b, "imag")))
            if (!invalidate(io, b)) return fail(err, "No se pudo escribir en la librería.");
    return true;
}

bool movePresetLibraryItem(const std::filesystem::path& file, const std::string& fromRaw, const std::string& toRaw,
                           std::string* err) {
    const std::string from = cleanPath(fromRaw), to = cleanPath(toRaw);
    if (badPath(from) || badPath(to)) return fail(err, "Ruta no válida.");
    PresetLibrary lib;
    if (!lib.open(file, err)) return false;
    if (lib.find(from) < 0) return fail(err, "No existe \"" + from + "\" en la librería.");
    if (lowerUtf8(from) == lowerUtf8(to)) {
        if (from == to) return true; // nada que hacer
    } else {
        if (lib.find(to) >= 0) return fail(err, "Ya hay un elemento \"" + to + "\" en la librería.");
        if (under(to, from)) return fail(err, "No se puede mover una carpeta dentro de sí misma.");
    }
    // Carpetas de destino que falten (registros nuevos, al final).
    const std::string toParent = parentOf(to);
    if (!toParent.empty() && lib.find(toParent) < 0) {
        if (!appendToPresetLibrary(file, {lib4d::NewEntry(toParent)}, err)) return false;
        if (!lib.open(file, err)) return false;
    }
    const PresetItem& top = lib.items()[lib.find(from)];
    const bool renamed = leafOf(from) != leafOf(to);

    std::vector<Block> blocks;
    if (!scanBlocks(file, blocks, err)) return false;
    LibState st;
    if (!scanLibrary(file, st, err)) return false;
    const auto ditm = lastOf(blocks, "ditm");
    std::ifstream in(file, std::ios::binary);

    std::vector<size_t> stale;
    Bytes records;
    uint32_t nextId = st.maxId + 1;
    // Una carpeta que solo existia como ruta de otros pasa a tener registro.
    if (top.id == 0) {
        putChunk(records, "ditm", nextId, folderRecord(nextId, st.rootUrl + "/" + to, leafOf(to)));
        ++nextId;
    }
    for (const PresetItem& it : lib.items()) {
        if (it.id <= 1 || !under(it.path, from)) continue;
        const auto d = ditm.find(it.id);
        if (d == ditm.end()) continue;
        Bytes body;
        lib4d::BcRecord rec;
        if (!readBody(in, blocks[d->second], body) || !lib4d::parseRecord(body, rec))
            return fail(err, "No se entiende el registro de \"" + it.path + "\"; la librería no se ha tocado.");
        const std::string newPath = to + it.path.substr(from.size());
        setUrl(rec.root, st.rootUrl + "/" + newPath);
        if (renamed && it.id == top.id) setNames(rec.root, leafOf(to));
        putChunk(records, "ditm", it.id, lib4d::serializeRecord(rec));
        stale.push_back(d->second);
    }
    in.close();
    putChunk(records, "dbfe", 0, {});
    std::fstream io(file, std::ios::in | std::ios::out | std::ios::binary);
    if (!io) return fail(err, "No se puede abrir la librería para escribir.");
    // Primero lo nuevo al final; despues se anulan los viejos. Si algo falla a
    // medias, como mucho queda el registro viejo y el nuevo con el mismo id
    // (manda el ultimo), nunca un elemento perdido.
    io.seekp(static_cast<std::streamoff>(st.endOffset));
    io.write(reinterpret_cast<const char*>(records.data()), static_cast<std::streamsize>(records.size()));
    if (!io) return fail(err, "No se pudo escribir en la librería.");
    io.flush();
    for (size_t i : stale)
        if (!invalidate(io, blocks[i])) return fail(err, "No se pudo escribir en la librería.");
    return true;
}

bool rewritePresetLibrary(const std::filesystem::path& src, const std::filesystem::path& dst,
                          const std::string& title, std::string* err) {
    std::error_code ec;
    if (std::filesystem::exists(dst, ec)) return fail(err, "Ya existe un archivo con ese nombre.");
    std::vector<Block> blocks;
    if (!scanBlocks(src, blocks, err)) return false;
    std::ifstream in(src, std::ios::binary);
    if (!in) return fail(err, "No se puede abrir la librería.");
    const std::u8string fname = dst.filename().u8string();
    const std::string newRoot = "preset://" + std::string(fname.begin(), fname.end());

    // Url raiz vieja (difm, clave 3).
    std::string oldRoot;
    lib4d::BcRecord header;
    bool haveHeader = false;
    for (const Block& b : blocks) {
        if (!isTag(b, "difm")) continue;
        Bytes body;
        if (readBody(in, b, body) && lib4d::parseRecord(body, header)) {
            haveHeader = true;
            if (const auto* u = header.root.find(3)) oldRoot = u->str();
        }
    }
    if (!haveHeader || oldRoot.rfind("preset://", 0) != 0)
        return fail(err, "La librería no tiene cabecera (difm) legible.");
    if (lib4d::BcValue* u = header.root.find(3)) u->setStr(newRoot);
    if (!title.empty())
        if (lib4d::BcValue* t = header.root.find(4))
            for (auto& kv : t->kids) kv.second.setStr(title);

    const auto ditm = lastOf(blocks, "ditm");
    const auto bina = lastOf(blocks, "bina");
    const auto imag = lastOf(blocks, "imag");
    // Posicion vieja de cada bina/imag (por cabecera) -> indice del bloque.
    std::map<uint64_t, size_t> byOffset;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (isTag(blocks[i], "bina") || isTag(blocks[i], "imag")) byOffset[blocks[i].offset] = i;

    std::ofstream o(dst, std::ios::binary);
    if (!o) return fail(err, "No se puede crear el archivo.");
    Bytes head = {'d', 'b', 'f', 'h'};
    head.resize(16, 0);
    // La cabecera de 16 bytes se copia tal cual (version del formato).
    in.clear();
    in.seekg(0);
    in.read(reinterpret_cast<char*>(head.data()), 16);
    o.write(reinterpret_cast<const char*>(head.data()), 16);
    uint64_t pos = 16;
    std::map<uint64_t, uint64_t> moved; // cabecera vieja -> nueva
    auto copyBlock = [&](const Block& b) -> bool {
        Bytes body;
        if (!readBody(in, b, body)) return false;
        Bytes chunk;
        putChunk(chunk, b.tag, b.id, body);
        o.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        moved[b.offset] = pos;
        pos += chunk.size();
        return static_cast<bool>(o);
    };
    auto cleanup = [&](const std::string& why) {
        o.close();
        std::error_code e2;
        std::filesystem::remove(dst, e2);
        return fail(err, why);
    };
    // Datos de los elementos vigentes (los que tienen registro), en el orden del archivo.
    std::set<size_t> wanted;
    std::map<uint32_t, lib4d::BcRecord> recs;
    for (const auto& [id, idx] : ditm) {
        Bytes body;
        lib4d::BcRecord rec;
        if (!readBody(in, blocks[idx], body) || !lib4d::parseRecord(body, rec))
            return cleanup("No se entiende un registro de la librería; no se ha escrito nada.");
        if (const auto* d = rec.root.find(14))
            if (const auto* off = d->find(0))
                if (auto it = byOffset.find(static_cast<uint64_t>(off->i)); it != byOffset.end()) wanted.insert(it->second);
        if (const auto* th = rec.root.find(5))
            for (const auto& kv : th->kids)
                if (const auto* off = kv.second.find(5))
                    if (auto it = byOffset.find(static_cast<uint64_t>(off->i)); it != byOffset.end()) wanted.insert(it->second);
        // Sin claves de posicion (librerias muy viejas): el ultimo bloque de su id.
        if (!rec.root.find(14))
            if (auto b = bina.find(id); b != bina.end()) wanted.insert(b->second);
        if (!rec.root.find(5))
            if (auto m = imag.find(id); m != imag.end()) wanted.insert(m->second);
        recs[id] = std::move(rec);
    }
    for (size_t i : wanted)
        if (!copyBlock(blocks[i])) return cleanup("No se pudo escribir el archivo.");

    Bytes tail;
    for (auto& [id, rec] : recs) {
        if (lib4d::BcValue* k6 = rec.root.find(6))
            if (lib4d::BcValue* u = k6->find(0)) {
                const std::string url = u->str();
                if (url.size() >= oldRoot.size() && lowerUtf8(url.substr(0, oldRoot.size())) == lowerUtf8(oldRoot))
                    setUrl(rec.root, newRoot + url.substr(oldRoot.size()));
            }
        if (lib4d::BcValue* d = rec.root.find(14))
            if (lib4d::BcValue* off = d->find(0))
                if (auto m = moved.find(static_cast<uint64_t>(off->i)); m != moved.end()) off->i = static_cast<int64_t>(m->second);
        if (lib4d::BcValue* th = rec.root.find(5))
            for (auto& kv : th->kids)
                if (lib4d::BcValue* off = kv.second.find(5))
                    if (auto m = moved.find(static_cast<uint64_t>(off->i)); m != moved.end()) off->i = static_cast<int64_t>(m->second);
        putChunk(tail, "ditm", id, lib4d::serializeRecord(rec));
    }
    putChunk(tail, "difm", 0, lib4d::serializeRecord(header));
    putChunk(tail, "dbfe", 0, {});
    o.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    if (!o) return cleanup("No se pudo escribir el archivo.");
    return true;
}

} // namespace iron3d
