#include "importers/lib4d/PresetLibrary.h"

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace iron3d {

namespace {

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

std::string utf16beToUtf8Impl(const uint8_t* p, size_t bytes) {
    std::string out;
    out.reserve(bytes / 2);
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        uint32_t c = (uint32_t(p[i]) << 8) | p[i + 1];
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
            const uint32_t lo = (uint32_t(p[i + 2]) << 8) | p[i + 3];
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(out, c);
    }
    return out;
}

bool readAt(std::ifstream& f, uint64_t off, void* dst, size_t n) {
    f.clear();
    f.seekg(std::streamoff(off));
    f.read(static_cast<char*>(dst), std::streamsize(n));
    return size_t(f.gcount()) == n;
}

// Un flujo zlib empieza por CMF=0x78 (deflate, ventana 32K) y la cabecera de
// dos bytes es multiplo de 31.
bool looksLikeZlib(const uint8_t* p) { return p[0] == 0x78 && ((p[0] << 8) | p[1]) % 31 == 0; }

constexpr uint64_t kMaxRecord = 16u << 20; // un ditm real ocupa < 2 KB

struct ChunkRef {
    uint64_t offset = 0, size = 0;
};

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

} // namespace

namespace lib4d {

std::string utf16beToUtf8(const uint8_t* p, size_t bytes) { return utf16beToUtf8Impl(p, bytes); }

// UTF-8 -> UTF-16BE (con pares sustitutos). Bytes invalidos -> U+FFFD.
std::vector<uint8_t> utf8ToUtf16be(const std::string& s) {
    std::vector<uint8_t> out;
    auto unit = [&](uint32_t u) {
        out.push_back(static_cast<uint8_t>(u >> 8));
        out.push_back(static_cast<uint8_t>(u));
    };
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
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

BcValue* BcValue::find(int32_t key) {
    for (auto& [k, v] : kids)
        if (k == key) return &v;
    return nullptr;
}
const BcValue* BcValue::find(int32_t key) const {
    for (const auto& [k, v] : kids)
        if (k == key) return &v;
    return nullptr;
}
std::string BcValue::str() const { return utf16beToUtf8Impl(utf16.data(), utf16.size()); }
void BcValue::setStr(const std::string& utf8) {
    type = 130;
    utf16 = utf8ToUtf16be(utf8);
}

namespace {

struct BcReader {
    const std::vector<uint8_t>& b;
    size_t i = 0;
    bool ok = true;
    bool need(size_t n) {
        if (i + n > b.size()) ok = false;
        return ok;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint32_t v = be32(b.data() + i);
        i += 4;
        return v;
    }
    int32_t tagged32() {
        if (!need(5) || b[i] != 0x0F) return ok = false, 0;
        ++i;
        return static_cast<int32_t>(u32());
    }
    // Contenedor; el 0x01 inicial ya consumido.
    void container(BcValue& v, int depth) {
        if (depth > 32) { ok = false; return; }
        v.type = 132;
        u32(); // 132
        u32(); // 1
        v.cid = tagged32();
        while (ok) {
            if (!need(1)) return;
            if (b[i] == 0x02) { ++i; return; }
            const int32_t key = tagged32();
            const int32_t type = tagged32();
            if (!ok || !need(1)) return;
            BcValue kid;
            kid.type = type;
            const uint8_t mark = b[i++];
            if (mark == 0x0F && type == 15) {
                kid.i = static_cast<int32_t>(u32());
            } else if (mark == 0x11 && type == 26) {
                if (!need(8)) return;
                kid.i = static_cast<int64_t>(be64(b.data() + i));
                i += 8;
            } else if (mark == 0x82 && type == 130) {
                const uint32_t n = u32();
                if (!need(n)) return;
                kid.utf16.assign(b.begin() + std::ptrdiff_t(i), b.begin() + std::ptrdiff_t(i + n));
                i += n;
            } else if (mark == 0x01 && type == 132) {
                container(kid, depth + 1);
            } else {
                ok = false; // un tipo que no sale en las librerias reales
                return;
            }
            v.kids.emplace_back(key, std::move(kid));
        }
    }
};

void putBe32(std::vector<uint8_t>& o, uint32_t v) {
    for (int sh = 24; sh >= 0; sh -= 8) o.push_back(static_cast<uint8_t>(v >> sh));
}
void putTagged(std::vector<uint8_t>& o, int32_t v) {
    o.push_back(0x0F);
    putBe32(o, static_cast<uint32_t>(v));
}
void writeContainer(std::vector<uint8_t>& o, const BcValue& v) {
    o.push_back(0x01);
    putBe32(o, 132);
    putBe32(o, 1);
    putTagged(o, v.cid);
    for (const auto& [key, kid] : v.kids) {
        putTagged(o, key);
        putTagged(o, kid.type);
        switch (kid.type) {
            case 26:
                o.push_back(0x11);
                putBe32(o, static_cast<uint32_t>(static_cast<uint64_t>(kid.i) >> 32));
                putBe32(o, static_cast<uint32_t>(kid.i));
                break;
            case 130:
                o.push_back(0x82);
                putBe32(o, static_cast<uint32_t>(kid.utf16.size()));
                o.insert(o.end(), kid.utf16.begin(), kid.utf16.end());
                break;
            case 132: writeContainer(o, kid); break;
            default: putTagged(o, static_cast<int32_t>(kid.i)); break;
        }
    }
    o.push_back(0x02);
}

} // namespace

bool parseRecord(const std::vector<uint8_t>& body, BcRecord& out) {
    if (body.size() < 14 || std::memcmp(body.data(), "QC4Dbrow", 8) != 0 || body[13] != 0x01) return false;
    out.header.assign(body.begin(), body.begin() + 13);
    out.root = BcValue{};
    BcReader r{body, 14};
    r.container(out.root, 0);
    return r.ok && r.i == body.size();
}

std::vector<uint8_t> serializeRecord(const BcRecord& rec) {
    std::vector<uint8_t> o = rec.header;
    writeContainer(o, rec.root);
    return o;
}

// Una cadena del contenedor: clave (0x0F + u32), tipo 130 (0x0F 00 00 00 82),
// marca 0x82, longitud u32 BE en bytes y el texto en UTF-16BE.
std::vector<std::string> recordStrings(const std::vector<uint8_t>& body, std::vector<size_t>* ends) {
    static const uint8_t kMark[] = {0x0F, 0x00, 0x00, 0x00, 0x82, 0x82};
    std::vector<std::string> out;
    if (ends) ends->clear();
    size_t i = 0;
    while (i + 10 <= body.size()) {
        auto it = std::search(body.begin() + std::ptrdiff_t(i), body.end(), std::begin(kMark), std::end(kMark));
        if (it == body.end()) break;
        const size_t at = size_t(it - body.begin());
        if (at + 10 > body.size()) break;
        const uint32_t len = be32(body.data() + at + 6);
        if (at + 10 + len > body.size()) break;
        out.push_back(utf16beToUtf8Impl(body.data() + at + 10, len));
        if (ends) ends->push_back(at + 10 + len);
        i = at + 10 + len;
    }
    return out;
}

// Clave 1 con tipo 15 (entero): 0x0F 00000001 0x0F 0000000F 0x0F <u32>.
uint32_t recordTypeId(const std::vector<uint8_t>& body, size_t start) {
    static const uint8_t kKey1[] = {0x0F, 0x00, 0x00, 0x00, 0x01, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x0F};
    if (start >= body.size()) return 0;
    auto it = std::search(body.begin() + std::ptrdiff_t(start), body.end(), std::begin(kKey1), std::end(kKey1));
    const size_t at = size_t(it - body.begin());
    if (it == body.end() || at + sizeof(kKey1) + 4 > body.size()) return 0;
    return be32(body.data() + at + sizeof(kKey1));
}

} // namespace lib4d

bool isLib4dPath(const std::filesystem::path& path) { return lowerAscii(path.extension().string()) == ".lib4d"; }

bool PresetLibrary::open(const std::filesystem::path& file, std::string* outError) {
    auto fail = [&](const std::string& why) {
        if (outError) *outError = why;
        return false;
    };
    file_ = file;
    title_.clear();
    rootUrl_.clear();
    items_.clear();

    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(file, ec);
    if (ec) return fail("No se puede leer el archivo.");
    std::ifstream f(file, std::ios::binary);
    if (!f) return fail("No se puede abrir el archivo.");
    uint8_t head[16];
    if (fileSize < 16 || !readAt(f, 0, head, 16) || std::memcmp(head, "dbfh", 4) != 0)
        return fail("No es una librería de preestablecidos de Cinema 4D (falta la cabecera dbfh).");

    std::map<uint32_t, std::vector<uint8_t>> records;
    std::map<uint32_t, ChunkRef> data, thumbs;
    std::map<uint64_t, uint32_t> binaAt, imagAt; // cabecera de bloque -> id (para las claves 14 y 5)
    std::vector<uint8_t> dbHeader;
    uint64_t off = 16;
    bool truncated = false;
    while (off + 16 <= fileSize) {
        uint8_t h[16];
        if (!readAt(f, off, h, 16)) break;
        const uint32_t id = be32(h + 4);
        const uint64_t size = be64(h + 8);
        if (size > fileSize - off - 16) {
            truncated = true;
            break;
        }
        const uint64_t body = off + 16;
        if (std::memcmp(h, "dbfe", 4) == 0) break;
        if (std::memcmp(h, "ditm", 4) == 0 || std::memcmp(h, "difm", 4) == 0) {
            if (size <= kMaxRecord) {
                std::vector<uint8_t> rec(static_cast<size_t>(size));
                if (!readAt(f, body, rec.data(), rec.size())) return fail("Error de lectura.");
                if (h[1] == 'i' && h[2] == 'f') dbHeader = std::move(rec);
                else records[id] = std::move(rec);
            }
        } else if (std::memcmp(h, "bina", 4) == 0) {
            data[id] = {body, size};
            binaAt[off] = id;
        } else if (std::memcmp(h, "imag", 4) == 0) {
            thumbs[id] = {body, size};
            imagAt[off] = id;
        }
        off = body + size;
    }
    if (records.empty())
        return fail(truncated ? "La librería está incompleta o dañada." : "La librería no contiene elementos.");

    if (!dbHeader.empty()) {
        lib4d::BcRecord hr;
        if (lib4d::parseRecord(dbHeader, hr)) {
            if (const auto* u = hr.root.find(3)) rootUrl_ = u->str();
            if (const auto* t = hr.root.find(4)) {
                const auto* es = t->find(lib4d::kLangEs);
                const auto* us = t->find(lib4d::kLangUs);
                title_ = es ? es->str() : us ? us->str() : !t->kids.empty() ? t->kids.front().second.str() : "";
            }
        } else {
            const auto s = lib4d::recordStrings(dbHeader);
            if (!s.empty()) rootUrl_ = s[0];
            if (s.size() > 1) title_ = s[1];
        }
    }
    auto blockLen = [&](uint64_t at, uint32_t id, const char* tag, ChunkRef& out) {
        uint8_t h[16];
        if (!readAt(f, at, h, 16) || std::memcmp(h, tag, 4) != 0 || be32(h + 4) != id) return false;
        out = {at + 16, be64(h + 8)};
        return out.size <= fileSize - out.offset;
    };

    std::set<std::string> folders;
    for (auto& [id, rec] : records) {
        // Gramatica real del registro; si no encaja (no ha pasado con ninguna
        // libreria real), se cae a buscar las cadenas como antes.
        lib4d::BcRecord br;
        const bool parsed = lib4d::parseRecord(rec, br);
        std::vector<size_t> ends;
        std::vector<std::string> s;
        std::string name;
        uint32_t typeId = 0;
        ChunkRef dataRef, thumbRef;
        if (parsed) {
            const auto* key6 = br.root.find(6);
            const auto* url0 = key6 ? key6->find(0) : nullptr;
            if (!url0 || url0->type != 130) continue;
            s.push_back(url0->str());
            if (const auto* names = br.root.find(16)) {
                const auto* es = names->find(lib4d::kLangEs);
                const auto* us = names->find(lib4d::kLangUs);
                name = es ? es->str() : us ? us->str() : !names->kids.empty() ? names->kids.front().second.str() : "";
            }
            if (const auto* t = br.root.find(1)) typeId = static_cast<uint32_t>(t->i);
            // Clave 14: {0 = posicion del bloque bina, 1 = compresion, 2 = tamano}.
            if (const auto* d = br.root.find(14))
                if (const auto* o = d->find(0)) blockLen(static_cast<uint64_t>(o->i), id, "bina", dataRef);
            // Clave 5: miniaturas {1 ancho, 2 alto, 5 posicion del bloque imag}; la mas grande.
            if (const auto* th = br.root.find(5)) {
                long long best = -1;
                for (const auto& [k, v] : th->kids) {
                    const auto *w = v.find(1), *hh = v.find(2), *o = v.find(5);
                    ChunkRef c;
                    if (!w || !hh || !o || !blockLen(static_cast<uint64_t>(o->i), id, "imag", c)) continue;
                    const long long area = (long long)w->i * hh->i;
                    if (area > best) { best = area; thumbRef = c; }
                }
            }
        } else {
            s = lib4d::recordStrings(rec, &ends);
            if (s.empty()) continue;
            if (s.size() > 1) name = s[1];
            typeId = lib4d::recordTypeId(rec, ends.size() > 1 ? ends[1] : 0);
        }
        // "preset://<lib>.lib4d/<ruta>" -> "<ruta>"
        const std::string& url = s[0];
        if (url.rfind("preset://", 0) != 0) continue;
        const size_t slash = url.find('/', 9);
        if (slash == std::string::npos) {
            if (rootUrl_.empty()) rootUrl_ = url;
            if (title_.empty()) title_ = !name.empty() ? name : url.substr(9);
            continue; // la raiz es la propia libreria
        }
        PresetItem it;
        it.id = id;
        it.path = url.substr(slash + 1);
        if (it.path.empty()) continue;
        const size_t last = it.path.rfind('/');
        it.parent = last == std::string::npos ? std::string() : it.path.substr(0, last);
        const std::string leaf = last == std::string::npos ? it.path : it.path.substr(last + 1);
        it.name = !name.empty() ? name : leaf;
        it.typeId = typeId;
        if (dataRef.size == 0)
            if (auto d = data.find(id); d != data.end()) dataRef = d->second;
        if (dataRef.size > 0) {
            it.dataOffset = dataRef.offset;
            it.dataSize = dataRef.size;
            uint8_t p[6] = {};
            if (it.dataSize >= 6 && readAt(f, it.dataOffset, p, 6) && looksLikeZlib(p + 4)) {
                it.compressed = true;
                it.rawSize = be32(p);
            }
            it.kind = it.typeId == kLib4dDocumentType ? PresetKind::Model : PresetKind::File;
        } else {
            it.kind = PresetKind::Folder;
            folders.insert(it.path);
        }
        if (thumbRef.size == 0)
            if (auto t = thumbs.find(id); t != thumbs.end()) thumbRef = t->second;
        it.thumbOffset = thumbRef.offset;
        it.thumbSize = thumbRef.size;
        items_.push_back(std::move(it));
    }
    // Carpetas que solo existen como ruta de otro elemento.
    for (size_t i = 0, n = items_.size(); i < n; ++i) {
        std::string p = items_[i].parent;
        while (!p.empty() && !folders.count(p)) {
            folders.insert(p);
            PresetItem dir;
            dir.path = p;
            const size_t last = p.rfind('/');
            dir.parent = last == std::string::npos ? std::string() : p.substr(0, last);
            dir.name = last == std::string::npos ? p : p.substr(last + 1);
            items_.push_back(dir);
            p = dir.parent;
        }
    }
    if (title_.empty()) title_ = file.stem().string();
    return true;
}

bool PresetLibrary::rootUrlMatchesFile() const {
    const std::u8string fn = file_.filename().u8string();
    return lowerAscii(rootUrl_) == lowerAscii("preset://" + std::string(fn.begin(), fn.end()));
}

int PresetLibrary::find(const std::string& path) const {
    const std::string key = lowerAscii(path);
    for (int i = 0; i < (int)items_.size(); ++i)
        if (lowerAscii(items_[i].path) == key) return i;
    return -1;
}

std::vector<int> PresetLibrary::children(const std::string& folder) const {
    std::vector<int> out;
    for (int i = 0; i < (int)items_.size(); ++i)
        if (items_[i].parent == folder) out.push_back(i);
    std::sort(out.begin(), out.end(), [&](int a, int b) {
        const bool fa = items_[a].kind == PresetKind::Folder, fb = items_[b].kind == PresetKind::Folder;
        if (fa != fb) return fa;
        return lowerAscii(items_[a].name) < lowerAscii(items_[b].name);
    });
    return out;
}

int PresetLibrary::modelCount() const {
    return (int)std::count_if(items_.begin(), items_.end(),
                              [](const PresetItem& i) { return i.kind == PresetKind::Model; });
}

bool PresetLibrary::readData(const PresetItem& item, std::vector<uint8_t>& out, std::string* outError) const {
    auto fail = [&](const char* why) {
        if (outError) *outError = why;
        out.clear();
        return false;
    };
    if (item.dataSize == 0) return fail("El elemento no tiene datos.");
    if (item.dataSize > 0x7FFFFFFFull || item.rawSize > 0x7FFFFFFFu) return fail("El elemento es demasiado grande.");
    std::ifstream f(file_, std::ios::binary);
    std::vector<uint8_t> raw(size_t(item.dataSize));
    if (!f || !readAt(f, item.dataOffset, raw.data(), raw.size())) return fail("Error de lectura de la librería.");
    if (!item.compressed) {
        out = std::move(raw);
        return true;
    }
    out.resize(item.rawSize);
    const int got = stbi_zlib_decode_buffer(reinterpret_cast<char*>(out.data()), (int)out.size(),
                                            reinterpret_cast<const char*>(raw.data() + 4), (int)raw.size() - 4);
    if (got != (int)item.rawSize) return fail("Los datos comprimidos del elemento están dañados.");
    return true;
}

bool PresetLibrary::readThumbnail(const PresetItem& item, std::vector<uint8_t>& rgba, int& width, int& height) const {
    if (!item.hasThumb() || item.thumbSize > (64u << 20)) return false;
    std::ifstream f(file_, std::ios::binary);
    std::vector<uint8_t> jpeg(size_t(item.thumbSize));
    if (!f || !readAt(f, item.thumbOffset, jpeg.data(), jpeg.size())) return false;
    int comp = 0;
    stbi_uc* px = stbi_load_from_memory(jpeg.data(), (int)jpeg.size(), &width, &height, &comp, 4);
    if (!px) return false;
    rgba.assign(px, px + size_t(width) * size_t(height) * 4);
    stbi_image_free(px);
    return true;
}

} // namespace iron3d
