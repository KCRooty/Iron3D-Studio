#pragma once

// FBX (2026-11-25): importar (binario y ASCII, con la libreria ufbx) con jerarquia, mallas triangulas,
// normales, UV, materiales y textura de color; exportar a FBX ASCII 7.4 con mallas, materiales y
// transformaciones locales. Sin animacion ni esqueleto todavia.

#include "core/Material.h"
#include "core/SceneNode.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct FbxReport {
    int nodes = 0;
    int meshes = 0;
    long long points = 0;
    long long polygons = 0;   // triangulos
    int materials = 0;
    std::vector<std::string> warnings;
};

bool isFbxPath(const std::filesystem::path& path);

bool importFbx(const std::filesystem::path& path, SceneNode& root, std::vector<Material>& materials,
               FbxReport& report, std::string* error);

bool exportFbx(const std::filesystem::path& path, const std::vector<const SceneNode*>& roots,
               const std::vector<Material>& materials, FbxReport& report, std::string* error);

} // namespace iron3d
