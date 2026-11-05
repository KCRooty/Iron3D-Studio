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

std::string utf16beToUtf8(const uint8_t* p, size_t bytes) {
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
        out.push_back(utf16beToUtf8(body.data() + at + 10, len));
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
        } else if (std::memcmp(h, "imag", 4) == 0) {
            thumbs[id] = {body, size};
        }
        off = body + size;
    }
    if (records.empty())
        return fail(truncated ? "La librería está incompleta o dañada." : "La librería no contiene elementos.");

    if (!dbHeader.empty()) {
        const auto s = lib4d::recordStrings(dbHeader);
        if (s.size() > 1) title_ = s[1];
    }

    std::set<std::string> folders;
    for (auto& [id, rec] : records) {
        std::vector<size_t> ends;
        const auto s = lib4d::recordStrings(rec, &ends);
        if (s.empty()) continue;
        // "preset://<lib>.lib4d/<ruta>" -> "<ruta>"
        const std::string& url = s[0];
        if (url.rfind("preset://", 0) != 0) continue;
        const size_t slash = url.find('/', 9);
        if (slash == std::string::npos) {
            if (title_.empty()) title_ = s.size() > 1 ? s[1] : url.substr(9);
            continue; // la raiz es la propia libreria
        }
        PresetItem it;
        it.id = id;
        it.path = url.substr(slash + 1);
        if (it.path.empty()) continue;
        const size_t last = it.path.rfind('/');
        it.parent = last == std::string::npos ? std::string() : it.path.substr(0, last);
        const std::string leaf = last == std::string::npos ? it.path : it.path.substr(last + 1);
        it.name = s.size() > 1 && !s[1].empty() ? s[1] : leaf;
        it.typeId = lib4d::recordTypeId(rec, ends.size() > 1 ? ends[1] : 0);
        if (auto d = data.find(id); d != data.end() && d->second.size > 0) {
            it.dataOffset = d->second.offset;
            it.dataSize = d->second.size;
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
        if (auto t = thumbs.find(id); t != thumbs.end()) {
            it.thumbOffset = t->second.offset;
            it.thumbSize = t->second.size;
        }
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
