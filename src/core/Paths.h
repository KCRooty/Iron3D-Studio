#pragma once

#include <filesystem>

namespace iron3d {

// Carpeta del ejecutable -- ahi viven shaders/ y scenes/ (ver CMakeLists.txt,
// VulkanRenderer.cpp y main.cpp). Portable Windows/Linux.
std::filesystem::path exeDirectory();

} // namespace iron3d
