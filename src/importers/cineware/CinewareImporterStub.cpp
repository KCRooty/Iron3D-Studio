// Version sin Cineware SDK del importador de .c4d (ver CinewareImporter.h).

#include "CinewareImporter.h"

#include <algorithm>
#include <cctype>

namespace iron3d {

bool cinewareAvailable() { return false; }

bool isC4DPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".c4d";
}

bool importC4D(const std::filesystem::path& /*file*/, SceneNode& /*parent*/, std::vector<Material>& /*materials*/,
               C4DImportReport& /*report*/, std::string* outError) {
    if (outError) {
#if defined(_WIN32)
        *outError = "Esta compilación de Iron3D no incluye el importador de Cinema 4D. "
                    "Se activa compilando con -DIRON3D_CINEWARE_SDK=<carpeta del Cineware SDK>.";
#else
        *outError = "El importador de Cinema 4D usa el Cineware SDK de MAXON, que solo existe para "
                    "Windows y macOS: en Linux no se puede abrir .c4d. Exporta la escena desde C4D a "
                    "otro formato o ábrela en Iron3D para Windows.";
#endif
    }
    return false;
}

namespace {
void noExport(std::string* outError) {
    if (outError)
        *outError = "Esta compilación de Iron3D no puede guardar .c4d: hace falta el Cineware SDK de MAXON "
                    "(solo Windows y macOS), que no viene incluido.";
}
} // namespace

bool exportC4D(const std::filesystem::path& /*file*/, const std::vector<const SceneNode*>& /*roots*/,
               const std::vector<Material>& /*materials*/, bool /*allMaterials*/, C4DExportReport& report,
               std::string* outError, int /*fps*/) {
    report = C4DExportReport{};
    noExport(outError);
    return false;
}

bool exportC4DToBytes(const std::vector<const SceneNode*>& /*roots*/, const std::vector<Material>& /*materials*/,
                      std::vector<uint8_t>& out, C4DExportReport& report, std::string* outError) {
    out.clear();
    report = C4DExportReport{};
    noExport(outError);
    return false;
}

} // namespace iron3d
