// Pruebas del lector de .lib4d (importers/lib4d/PresetLibrary). Sin GPU ni
// Cineware: se ejecutan en el CI de Windows y Linux con una libreria
// sintetica construida aqui mismo con el formato observado.
//
// Con argumentos, ademas abre esas librerias reales y comprueba que cada
// modelo se descomprime a un .c4d y que las miniaturas se decodifican:
//   preset_library_test "ruta/a/libreria.lib4d" ...

#include "importers/lib4d/PresetLibrary.h"
#include "importers/lib4d/PresetLibraryWriter.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include <stb_image_write.h> // implementacion en iron3d_lib4d (StbImpl.cpp)
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string>
#include <vector>

using namespace iron3d;
using Bytes = std::vector<uint8_t>;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FALLO: %s\n", what);
    }
}

void put32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
}

void put64(Bytes& b, uint64_t v) {
    put32(b, uint32_t(v >> 32));
    put32(b, uint32_t(v));
}

void putString(Bytes& b, const char* ascii) {
    const uint8_t mark[] = {0x0F, 0x00, 0x00, 0x00, 0x82, 0x82};
    b.insert(b.end(), std::begin(mark), std::end(mark));
    put32(b, uint32_t(std::strlen(ascii) * 2));
    for (const char* p = ascii; *p; ++p) {
        b.push_back(0);
        b.push_back(uint8_t(*p));
    }
}

void putType(Bytes& b, uint32_t type) {
    const uint8_t key1[] = {0x0F, 0x00, 0x00, 0x00, 0x01, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x0F};
    b.insert(b.end(), std::begin(key1), std::end(key1));
    put32(b, type);
}

// Registro con la forma de los reales: cabecera de contenedor, url, nombre
// localizado y despues el tipo. Delante del nombre va un "clave 1" falso con
// otro valor (los registros reales lo tienen y no es el tipo).
Bytes record(const std::string& url, const char* name, uint32_t type) {
    Bytes b = {'Q', 'C', '4', 'D', 'b', 'r', 'o', 'w', 0, 0, 'J', 'P'};
    putString(b, url.c_str());
    putType(b, 2);
    if (name) putString(b, name);
    if (type) putType(b, type);
    b.push_back(2);
    return b;
}

void chunk(Bytes& out, const char* tag, uint32_t id, const Bytes& body) {
    out.insert(out.end(), tag, tag + 4);
    put32(out, id);
    put64(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
}

uint32_t adler32(const Bytes& d) {
    uint32_t a = 1, b = 0;
    for (uint8_t c : d) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// u32 BE con el tamano + flujo zlib con bloques "stored" (sin comprimir):
// valido para cualquier inflador y no hace falta un compresor en la prueba.
Bytes zlibStored(const Bytes& d) {
    Bytes b;
    put32(b, uint32_t(d.size()));
    b.push_back(0x78);
    b.push_back(0x01);
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, d.size() - pos);
        b.push_back(pos + n == d.size() ? 1 : 0);
        b.push_back(uint8_t(n));
        b.push_back(uint8_t(n >> 8));
        b.push_back(uint8_t(~n));
        b.push_back(uint8_t(~n >> 8));
        b.insert(b.end(), d.begin() + std::ptrdiff_t(pos), d.begin() + std::ptrdiff_t(pos + n));
        pos += n;
    } while (pos < d.size());
    put32(b, adler32(d));
    return b;
}

Bytes tinyJpeg() {
    unsigned char px[8 * 8 * 3];
    for (int i = 0; i < 8 * 8; ++i) {
        px[i * 3 + 0] = 200;
        px[i * 3 + 1] = 40;
        px[i * 3 + 2] = 40;
    }
    Bytes out;
    stbi_write_jpg_to_func(
        [](void* ctx, void* data, int size) {
            auto* o = static_cast<Bytes*>(ctx);
            o->insert(o->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &out, 8, 8, 3, px, 90);
    return out;
}

void syntheticLibrary() {
    const std::string base = "preset://prueba.lib4d";
    Bytes doc = {'Q', 'C', '4', 'D', 'C', '4', 'D', '6'};
    for (int i = 0; i < 150000; ++i) doc.push_back(uint8_t(i * 7)); // > 64 KB: varios bloques
    const Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 1, 2, 3};

    Bytes lib = {'d', 'b', 'f', 'h', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    chunk(lib, "difm", 0, record(base, "PRUEBA", 0));
    chunk(lib, "bina", 3, png);
    chunk(lib, "imag", 6, tinyJpeg());
    chunk(lib, "bina", 6, zlibStored(Bytes(16, 0))); // version vieja del modelo...
    chunk(lib, "ivld", 9, record(base + "/Borrado", "Borrado", 1018107));
    chunk(lib, "bina", 6, zlibStored(doc));          // ...manda la ultima
    chunk(lib, "ditm", 2, record(base + "/tex", "tex", 0));
    chunk(lib, "ditm", 6, record(base + "/Modelo", "Modelo", kLib4dDocumentType));
    chunk(lib, "ditm", 3, record(base + "/tex/piel.png", "piel.png", 1018031));
    chunk(lib, "ditm", 7, record(base + "/Personajes/Extra/Otro", "Otro", kLib4dDocumentType));
    chunk(lib, "bina", 7, zlibStored(doc));
    chunk(lib, "ditm", 1, record(base, nullptr, 0));
    chunk(lib, "dbfe", 0, {});

    const auto path = std::filesystem::temp_directory_path() / "iron3d_preset_library_test.lib4d";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(lib.data()), std::streamsize(lib.size()));
    }
    PresetLibrary L;
    std::string err;
    check(L.open(path, &err), "abrir la libreria sintetica");
    check(L.title() == "PRUEBA", "titulo de la libreria (difm)");
    check(L.modelCount() == 2, "dos modelos (el anulado no cuenta)");

    const PresetItem* model = nullptr;
    const PresetItem* tex = nullptr;
    for (const PresetItem& it : L.items()) {
        if (it.path == "Modelo") model = &it;
        if (it.path == "tex/piel.png") tex = &it;
    }
    check(model && model->kind == PresetKind::Model && model->compressed, "modelo comprimido");
    check(model && model->hasThumb(), "miniatura del modelo");
    check(tex && tex->kind == PresetKind::File && !tex->compressed && tex->parent == "tex", "textura sin comprimir");

    const auto root = L.children("");
    check(root.size() == 3, "raiz: tex, Personajes y Modelo");
    check(!root.empty() && L.items()[root[0]].kind == PresetKind::Folder, "carpetas primero");
    check(L.children("Personajes").size() == 1, "carpeta implicita Personajes/Extra");

    Bytes out;
    check(model && L.readData(*model, out, &err) && out == doc, "modelo descomprimido identico (ultima version)");
    check(tex && L.readData(*tex, out, &err) && out == png, "textura tal cual");
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    check(model && L.readThumbnail(*model, rgba, w, h) && w == 8 && h == 8 && rgba.size() == 8 * 8 * 4,
          "miniatura JPEG decodificada");
    check(!rgba.empty() && rgba[0] > 150 && rgba[1] < 100, "color de la miniatura");

    check(!L.open(path.parent_path() / "no_existe.lib4d", &err), "archivo inexistente");
    Bytes junk = {'n', 'o', 'p', 'e'};
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(junk.data()), std::streamsize(junk.size()));
    }
    check(!L.open(path, &err), "archivo que no es .lib4d");
    std::filesystem::remove(path);
    check(isLib4dPath("a/B.LIB4D") && !isLib4dPath("a/b.c4d"), "extension .lib4d");
}

void realLibrary(const char* p) {
    PresetLibrary L;
    std::string err;
    if (!L.open(std::filesystem::path(reinterpret_cast<const char8_t*>(p)), &err)) {
        std::printf("FALLO: %s: %s\n", p, err.c_str());
        ++g_failures;
        return;
    }
    int models = 0, files = 0, folders = 0, thumbs = 0, docsOk = 0;
    bool thumbOk = false;
    for (const PresetItem& it : L.items()) {
        if (it.kind == PresetKind::Folder) ++folders;
        if (it.kind == PresetKind::File) ++files;
        if (it.hasThumb()) ++thumbs;
        if (it.kind != PresetKind::Model) continue;
        ++models;
        Bytes d;
        if (L.readData(it, d, &err) && d.size() > 8 && std::memcmp(d.data(), "QC4DC4D6", 8) == 0) ++docsOk;
        else std::printf("  modelo '%s': %s\n", it.name.c_str(), err.c_str());
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!thumbOk) thumbOk = L.readThumbnail(it, rgba, w, h);
    }
    std::printf("%s: \"%s\" modelos=%d (%d .c4d ok) archivos=%d carpetas=%d miniaturas=%d\n", p, L.title().c_str(),
                models, docsOk, files, folders, thumbs);
    check(models > 0 && docsOk == models, "todos los modelos se descomprimen a .c4d");
    check(thumbOk, "alguna miniatura se decodifica");

    // Cada registro (difm/ditm) se lee con la gramatica y se vuelve a escribir
    // identico, byte a byte: lo que permite renombrar y mover sin perder nada.
    std::ifstream f(L.file(), std::ios::binary);
    f.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(f.tellg());
    uint64_t off = 16;
    int recs = 0, same = 0;
    while (off + 16 <= size) {
        uint8_t h[16];
        f.seekg(static_cast<std::streamoff>(off));
        f.read(reinterpret_cast<char*>(h), 16);
        uint64_t len = 0;
        for (int k = 8; k < 16; ++k) len = (len << 8) | h[k];
        if (std::memcmp(h, "dbfe", 4) == 0) break;
        const bool rec = std::memcmp(h, "ditm", 4) == 0 || std::memcmp(h, "difm", 4) == 0 ||
                         (std::memcmp(h, "ivld", 4) == 0 && len < (1u << 20));
        if (rec && len < (1u << 20)) {
            Bytes body(static_cast<size_t>(len));
            f.read(reinterpret_cast<char*>(body.data()), static_cast<std::streamsize>(len));
            if (body.size() > 8 && std::memcmp(body.data(), "QC4Dbrow", 8) == 0) {
                ++recs;
                lib4d::BcRecord r;
                if (lib4d::parseRecord(body, r) && lib4d::serializeRecord(r) == body) ++same;
            }
        }
        off += 16 + len;
    }
    std::printf("  registros: %d, identicos al reescribirlos: %d\n", recs, same);
    check(recs > 0 && same == recs, "todos los registros reales se reescriben identicos");
}

// Escritor: crear, anadir y volver a leer con el lector propio.
void writerRoundTrip() {
    // Hash de la url: valor sacado de una libreria real (FNaF 2 Light Pack).
    check(lib4d::urlHash("preset://FNaF 2 Light Pack.lib4d/FNaF 2 Light Pack.lib4d/In Office/Toy Chica In Office") ==
              -411950167,
          "hash de la url (CRC-32 de la url en minusculas, UTF-16BE)");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "iron3d_writer_test.lib4d";
    std::filesystem::remove(path);
    std::string err;
    check(createPresetLibrary(path, "Mi Librería", &err), "crear libreria vacia");
    check(!createPresetLibrary(path, "Otra", &err), "no pisa una libreria que ya existe");
    PresetLibrary L;
    check(L.open(path, &err) && L.title() == "Mi Librería" && L.items().empty(), "libreria vacia se lee");

    Bytes doc = {'Q', 'C', '4', 'D', 'C', '4', 'D', '6'};
    for (int i = 0; i < 5000; ++i) doc.push_back(static_cast<uint8_t>(i * 7));
    Bytes png;
    const uint8_t px[4 * 4 * 3] = {255, 0, 0};
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* b = static_cast<Bytes*>(ctx);
            b->insert(b->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &png, 4, 4, 3, px, 4 * 3);

    check(appendToPresetLibrary(path, {{"Personajes/Freddy", doc}, {"tex/piel.png", png}, {"Vacía", {}}}, &err),
          "anadir modelo, textura y carpeta");
    check(!appendToPresetLibrary(path, {{"Personajes/freddy", doc}}, &err), "no repite una ruta (sin mayusculas)");
    check(appendToPresetLibrary(path, {{"Personajes/Bonnie", doc}}, &err), "anadir otra vez (al final)");

    check(L.open(path, &err), "reabrir tras anadir");
    const PresetItem *freddy = nullptr, *bonnie = nullptr, *tex = nullptr;
    int folders = 0;
    for (const PresetItem& it : L.items()) {
        if (it.path == "Personajes/Freddy") freddy = &it;
        if (it.path == "Personajes/Bonnie") bonnie = &it;
        if (it.path == "tex/piel.png") tex = &it;
        if (it.kind == PresetKind::Folder) ++folders;
    }
    check(folders == 3, "carpetas Personajes, tex y Vacía");
    check(freddy && freddy->kind == PresetKind::Model && freddy->compressed && freddy->name == "Freddy",
          "modelo comprimido con su nombre");
    check(bonnie && bonnie->kind == PresetKind::Model, "segundo modelo");
    check(tex && tex->kind == PresetKind::File && !tex->compressed, "textura tal cual");
    Bytes out;
    check(freddy && L.readData(*freddy, out, &err) && out == doc, "modelo identico al leerlo");
    check(bonnie && L.readData(*bonnie, out, &err) && out == doc, "segundo modelo identico");
    check(tex && L.readData(*tex, out, &err) && out == png, "textura identica");
    check(L.children("").size() == 3, "raiz: Personajes, tex, Vacía");
    // Texto que no es UTF-8 valido (p.ej. "Añadidos" en ANSI) no se come
    // las letras siguientes: cada byte malo sale como un solo caracter.
    check(appendToPresetLibrary(path, {{std::string("A\xF1" "adidos/Cubo"), doc}}, &err) && L.open(path, &err),
          "anadir con nombre no UTF-8");
    bool kept = false;
    for (const PresetItem& it : L.items()) kept = kept || it.path.find("adidos/Cubo") != std::string::npos;
    check(kept, "byte no UTF-8 sustituido sin perder letras");

    // Modelo con miniatura: bloque "imag" en JPEG que el lector encuentra.
    {
        lib4d::NewEntry e("Con miniatura", doc);
        e.thumbWidth = e.thumbHeight = 16;
        e.thumbRgba.assign(16 * 16 * 4, 255);
        for (size_t i = 0; i < e.thumbRgba.size(); i += 4) e.thumbRgba[i + 1] = e.thumbRgba[i + 2] = 40; // rojo
        check(appendToPresetLibrary(path, {e}, &err) && L.open(path, &err), "anadir modelo con miniatura");
        const PresetItem* withThumb = nullptr;
        for (const PresetItem& it : L.items())
            if (it.path == "Con miniatura") withThumb = &it;
        Bytes rgba;
        int w = 0, h = 0;
        check(withThumb && withThumb->hasThumb() && L.readThumbnail(*withThumb, rgba, w, h) && w == 16 && h == 16 &&
                  rgba.size() == 16u * 16u * 4u && rgba[0] > 200 && rgba[1] < 90,
              "miniatura JPEG legible y roja");
        check(withThumb && L.readData(*withThumb, out, &err) && out == doc, "modelo con miniatura identico");
    }

    // Renombrar: mismo id, datos intactos, nombre visible nuevo.
    auto at = [&](const char* p) -> const PresetItem* {
        const int i = L.find(p);
        return i < 0 ? nullptr : &L.items()[i];
    };
    const uint32_t freddyId = at("Personajes/Freddy") ? at("Personajes/Freddy")->id : 0;
    check(movePresetLibraryItem(path, "Personajes/Freddy", "Personajes/Freddy Viejo", &err) && L.open(path, &err),
          "renombrar modelo");
    check(!at("Personajes/Freddy") && at("Personajes/Freddy Viejo") && at("Personajes/Freddy Viejo")->id == freddyId &&
              at("Personajes/Freddy Viejo")->name == "Freddy Viejo",
          "renombrado: ruta y nombre nuevos, mismo id");
    check(at("Personajes/Freddy Viejo") && L.readData(*at("Personajes/Freddy Viejo"), out, &err) && out == doc,
          "renombrado: datos intactos");
    check(!movePresetLibraryItem(path, "Personajes/Bonnie", "Personajes/Freddy Viejo", &err), "no pisa otro elemento");
    check(!movePresetLibraryItem(path, "Personajes", "Personajes/Sub", &err), "no mete una carpeta en si misma");

    // Mover una carpeta con contenido a otra que no existe (se crea).
    check(movePresetLibraryItem(path, "Personajes", "Archivo/2026/Personajes", &err) && L.open(path, &err),
          "mover carpeta con contenido");
    check(!at("Personajes") && at("Archivo/2026") && at("Archivo/2026/Personajes/Bonnie") &&
              at("Archivo/2026/Personajes/Freddy Viejo"),
          "movida: el contenido va con la carpeta y se crean las intermedias");
    check(at("Archivo/2026/Personajes/Bonnie") && L.readData(*at("Archivo/2026/Personajes/Bonnie"), out, &err) &&
              out == doc,
          "movida: datos intactos");
    check(at("Con miniatura") && at("Con miniatura")->hasThumb(), "la miniatura sigue tras otras ediciones");

    // Borrar un archivo y una carpeta entera.
    check(removeFromPresetLibrary(path, {"tex/piel.png", "Archivo/2026/Personajes"}, &err) && L.open(path, &err),
          "borrar archivo y carpeta");
    check(!at("tex/piel.png") && !at("Archivo/2026/Personajes") && !at("Archivo/2026/Personajes/Bonnie") &&
              at("Archivo/2026") && at("Con miniatura"),
          "borrado: solo lo pedido");
    check(!removeFromPresetLibrary(path, {"No existe"}, &err), "borrar algo que no existe falla");

    // Reescribir con otro nombre: url raiz nueva, sin bloques anulados, todo legible.
    const std::filesystem::path copy = std::filesystem::temp_directory_path() / "iron3d writer copia.lib4d";
    std::filesystem::remove(copy);
    check(rewritePresetLibrary(path, copy, "Copia", &err), "reescribir con otro nombre");
    PresetLibrary C;
    check(C.open(copy, &err) && C.rootUrl() == "preset://iron3d writer copia.lib4d" && C.rootUrlMatchesFile() &&
              C.title() == "Copia",
          "copia: url raiz y titulo nuevos");
    check(C.items().size() == L.items().size(), "copia: mismos elementos");
    const int cm = C.find("Con miniatura");
    Bytes rgba;
    int w = 0, h = 0;
    check(cm >= 0 && C.readData(C.items()[cm], out, &err) && out == doc && C.readThumbnail(C.items()[cm], rgba, w, h),
          "copia: modelo y miniatura en su nueva posicion");
    check(std::filesystem::file_size(copy) < std::filesystem::file_size(path), "copia: sin bloques anulados");
    check(!rewritePresetLibrary(path, copy, "", &err), "reescribir no pisa un archivo");
    std::filesystem::remove(copy);
    std::filesystem::remove(path);
}

// "--write salida.lib4d Titulo ruta=archivo ... carpeta/": escribe una libreria
// para validarla fuera (con el propio Cinema 4D R19 y el extractor).
int writeMode(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::filesystem::path out(reinterpret_cast<const char8_t*>(argv[2]));
    std::string err;
    // Si ya existe, solo se le anade (prueba del anadido al final).
    if (!std::filesystem::exists(out) && !createPresetLibrary(out, argv[3], &err)) {
        std::printf("FALLO: %s\n", err.c_str());
        return 1;
    }
    std::vector<lib4d::NewEntry> entries;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        lib4d::NewEntry e;
        const size_t eq = a.find('=');
        if (eq == std::string::npos) {
            e.path = a;
            if (!e.path.empty() && e.path.back() == '/') e.path.pop_back();
        } else {
            e.path = a.substr(0, eq);
            std::ifstream f(std::filesystem::path(reinterpret_cast<const char8_t*>(a.c_str() + eq + 1)), std::ios::binary);
            e.data.assign(std::istreambuf_iterator<char>(f), {});
        }
        entries.push_back(std::move(e));
    }
    if (!appendToPresetLibrary(out, entries, &err)) {
        std::printf("FALLO: %s\n", err.c_str());
        return 1;
    }
    std::printf("escrita %s\n", argv[2]);
    return 0;
}

// "--move lib de a", "--remove lib ruta...", "--rewrite origen destino [titulo]":
// ediciones para validarlas fuera con Cinema 4D R19.
int editMode(int argc, char** argv) {
    auto u8 = [](const char* s) { return std::filesystem::path(reinterpret_cast<const char8_t*>(s)); };
    const std::string mode = argv[1];
    std::string err;
    bool ok = false;
    if (mode == "--move" && argc == 5) {
        ok = movePresetLibraryItem(u8(argv[2]), argv[3], argv[4], &err);
    } else if (mode == "--remove" && argc >= 4) {
        ok = removeFromPresetLibrary(u8(argv[2]), std::vector<std::string>(argv + 3, argv + argc), &err);
    } else if (mode == "--rewrite" && argc >= 4) {
        ok = rewritePresetLibrary(u8(argv[2]), u8(argv[3]), argc > 4 ? argv[4] : "", &err);
    } else {
        err = "argumentos";
    }
    std::printf("%s %s\n", ok ? "hecho" : "FALLO:", ok ? mode.c_str() : err.c_str());
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--write") return writeMode(argc, argv);
    if (argc > 1 && (std::string(argv[1]) == "--move" || std::string(argv[1]) == "--remove" ||
                     std::string(argv[1]) == "--rewrite"))
        return editMode(argc, argv);
    syntheticLibrary();
    writerRoundTrip();
    for (int i = 1; i < argc; ++i) realLibrary(argv[i]);
    if (g_failures == 0) std::printf("preset_library: todo correcto\n");
    return g_failures == 0 ? 0 : 1;
}
