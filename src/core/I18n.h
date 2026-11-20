#pragma once

// Idiomas de la interfaz (2026-11-20), como en Cinema 4D: el idioma se elige en
// Preferencias > Interfaz y en el instalador, y se aplica al reiniciar.
//
// Las claves del diccionario son los textos en espanol que hay en el codigo
// ("Mover", "Configuracion del Proyecto..."), la lengua en la que se escribio
// Iron3D. Cada idioma es un archivo UTF-8 `lang/<codigo>.txt` junto al
// ejecutable, con lineas `clave<TAB>traduccion` (escapes \n \t \\). Los
// nombres de los comandos salen de los strings_* oficiales de Cinema 4D (asi
// "Reproducir Hacia Delante" es exactamente "Play Forwards" en R19); el resto
// los traduce el proyecto.
//
// El idioma por defecto es el ingles ("en"): es contra lo que se compara con
// C4D. Si a un texto le falta traduccion se usa el ingles y, en ultimo caso,
// la clave (espanol). "es" es la identidad: no carga ningun archivo.

#include <string>
#include <vector>

namespace iron3d {

struct LanguageInfo {
    const char* code;        // "en", "es", "de"...
    const char* nativeName;  // en su propio idioma, UTF-8
    bool cjk;                // necesita una fuente CJK del sistema
    bool cyrillic;
};

namespace i18n {

const std::vector<LanguageInfo>& languages();
const LanguageInfo* findLanguage(const std::string& code);

// Idioma con el que arranca el programa: el guardado en preferencias si existe;
// si no, el de `language.cfg` (lo escribe el instalador); si no, "en".
std::string resolveStartupLanguage(const std::string& prefsLanguage);

// Carga `lang/<code>.txt` (y el ingles como respaldo). Devuelve false si el
// codigo no existe; en ese caso se queda en ingles.
bool setLanguage(const std::string& code);
const std::string& currentLanguage();

// Traduce una clave (texto en espanol). Idempotente y barata; si no hay
// traduccion devuelve la propia clave. Una parte "##id" final se conserva.
const char* translate(const char* key);

} // namespace i18n

// Texto traducible que se resuelve al USARLO (no al declararlo): permite
// tablas constantes `{ "Cubo", ... }` cuyo texto cambia con el idioma.
struct Tr {
    const char* key;
    constexpr Tr(const char* k) : key(k) {}
    operator const char*() const { return i18n::translate(key); }
};

inline const char* tr(const char* key) { return i18n::translate(key); }
std::string trf(const char* fmtKey, ...); // snprintf con la plantilla ya traducida

} // namespace iron3d
