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

// "Guardar como": `defaultName` sale ya escrito y `defaultExt` (sin punto) se
// anade si no se pone extension. Pregunta antes de sobrescribir.
std::optional<std::filesystem::path> saveFileDialog(const std::string& title,
                                                    const std::vector<std::pair<std::string, std::string>>& filters,
                                                    const std::string& defaultName, const std::string& defaultExt);

// Igual, pero se pueden elegir varios archivos a la vez (vacio = cancelado).
std::vector<std::filesystem::path> openFilesDialog(const std::string& title,
                                                   const std::vector<std::pair<std::string, std::string>>& filters);

// "¿Quiere guardar los cambios...?" con Si / No / Cancelar (como el de Cinema 4D). Solo en Windows;
// en el resto devuelve No (no hay dialogo del sistema).
enum class SaveChoice { Yes, No, Cancel };
SaveChoice askSaveChanges(const std::string& title, const std::string& text);

// Abre una carpeta en el explorador de archivos del sistema (Windows; en el resto no hace nada). true = lanzado.
bool openFolderInExplorer(const std::filesystem::path& dir);
// Abre una direccion web (http/https) en el navegador por defecto (Windows; en el resto no hace nada). true = lanzado.
bool openUrl(const std::string& url);
// Argumentos de la linea de comandos en UTF-8 (en Windows argv viene en ANSI). Vacio = usar argv tal cual.
std::vector<std::string> commandLineUtf8();

} // namespace iron3d
