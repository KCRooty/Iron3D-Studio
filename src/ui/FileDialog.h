#pragma once

// Dialogo nativo de "Abrir archivo" (Archivo > Abrir / Fusionar). En Windows
// es el del sistema (comdlg32); en el resto todavia no hay ninguno y la UI
// usa su dialogo propio de la carpeta scenes/.

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace iron3d {

bool nativeFileDialogAvailable();

// `filters`: pares (descripcion, patrones separados por ';'), p. ej.
// {"Escenas", "*.iron;*.c4d"}. Devuelve nullopt si se cancela.
std::optional<std::filesystem::path> openFileDialog(const std::string& title,
                                                    const std::vector<std::pair<std::string, std::string>>& filters);

// Igual, pero se pueden elegir varios archivos a la vez (vacio = cancelado).
std::vector<std::filesystem::path> openFilesDialog(const std::string& title,
                                                   const std::vector<std::pair<std::string, std::string>>& filters);

} // namespace iron3d
