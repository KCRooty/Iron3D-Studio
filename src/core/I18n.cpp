#include "I18n.h"

#include "Paths.h"

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <unordered_map>

namespace iron3d {
namespace i18n {

namespace {

struct SvHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using Dict = std::unordered_map<std::string, std::string, SvHash, std::equal_to<>>;

Dict g_dict;                 // idioma activo (ya mezclado con el ingles de respaldo)
Dict g_suffixCache;          // "Texto##id" -> "Translated##id" (almacenamiento estable)
std::string g_lang = "en";
bool g_identity = false;     // espanol: devuelve la clave tal cual

const std::vector<LanguageInfo> kLanguages = {
    {"en", "English", false, false},
    {"es", "Espa\xC3\xB1ol", false, false},
    {"de", "Deutsch", false, false},
    {"fr", "Fran\xC3\xA7" "ais", false, false},
    {"it", "Italiano", false, false},
    {"pl", "Polski", false, false},
    {"cs", "\xC4\x8C" "e\xC5\xA1tina", false, false},
    {"ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", false, true},
    {"ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", true, false},
    {"ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", true, false},
    {"zh", "\xE4\xB8\xAD\xE6\x96\x87 (\xE7\xAE\x80\xE4\xBD\x93)", true, false},
};

std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Lee un archivo de idioma y lo vuelca en `into` (las claves repetidas se sobrescriben).
bool loadFile(const std::string& code, Dict& into) {
    std::ifstream f(exeDirectory() / "lang" / (code + ".txt"), std::ios::binary);
    if (!f) return false;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line.erase(0, 3); // BOM
        first = false;
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        into[unescape(std::string_view(line).substr(0, tab))] = unescape(std::string_view(line).substr(tab + 1));
    }
    return true;
}

} // namespace

const std::vector<LanguageInfo>& languages() { return kLanguages; }

const LanguageInfo* findLanguage(const std::string& code) {
    for (const LanguageInfo& l : kLanguages)
        if (code == l.code) return &l;
    return nullptr;
}

std::string resolveStartupLanguage(const std::string& prefsLanguage) {
    if (findLanguage(prefsLanguage)) return prefsLanguage;
    std::ifstream f(exeDirectory() / "language.cfg");
    std::string code;
    if (f && std::getline(f, code)) {
        while (!code.empty() && (code.back() == '\r' || code.back() == ' ')) code.pop_back();
        if (findLanguage(code)) return code;
    }
    return "en";
}

bool setLanguage(const std::string& code) {
    g_dict.clear();
    g_suffixCache.clear();
    if (!findLanguage(code)) {
        g_lang = "en";
        g_identity = false;
        loadFile("en", g_dict);
        return false;
    }
    g_lang = code;
    g_identity = code == "es";
    if (g_identity) return true;
    if (code != "en") loadFile("en", g_dict); // respaldo
    loadFile(code, g_dict);
    return true;
}

const std::string& currentLanguage() { return g_lang; }

const char* translate(const char* key) {
    if (!key || g_identity || g_dict.empty()) return key;
    const std::string_view k(key);
    if (auto it = g_dict.find(k); it != g_dict.end()) return it->second.c_str();
    const size_t hash = k.find("##");
    if (hash == std::string_view::npos || hash == 0) return key;
    if (auto c = g_suffixCache.find(k); c != g_suffixCache.end()) return c->second.c_str();
    auto it = g_dict.find(k.substr(0, hash));
    if (it == g_dict.end()) return key;
    return g_suffixCache.emplace(std::string(k), it->second + std::string(k.substr(hash))).first->second.c_str();
}

} // namespace i18n

std::string trf(const char* fmtKey, ...) {
    const char* fmt = i18n::translate(fmtKey);
    va_list a, b;
    va_start(a, fmtKey);
    va_copy(b, a);
    const int n = std::vsnprintf(nullptr, 0, fmt, a);
    va_end(a);
    std::string out((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) std::vsnprintf(out.data(), (size_t)n + 1, fmt, b);
    va_end(b);
    return out;
}

} // namespace iron3d
