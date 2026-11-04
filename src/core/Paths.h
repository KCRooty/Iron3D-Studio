#pragma once

#include <filesystem>
#include <string>

namespace iron3d {

// Ruta en UTF-8 como std::string (en C++20 u8string() devuelve std::u8string).
inline std::string pathUtf8(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Carpeta del ejecutable -- ahi viven shaders/ y scenes/ (ver CMakeLists.txt,
// VulkanRenderer.cpp y main.cpp). Portable Windows/Linux.
std::filesystem::path exeDirectory();

} // namespace iron3d
