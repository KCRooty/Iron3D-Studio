#pragma once

// glTF 2.0 (.glb y .gltf) (2026-11-19): importar con jerarquia, mallas, materiales PBR basicos y
// texturas externas; exportar a .glb (un solo archivo) con la transformacion de mundo de cada
// malla. Sin dependencias: lleva su propio lector de JSON. No hace falta Cineware.

#include "core/Material.h"
#include "core/SceneNode.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct GltfReport {
    int nodes = 0;
    int meshes = 0;
    long long points = 0;
    long long polygons = 0;   // triangulos
    int materials = 0;
    std::vector<std::string> warnings;
};

bool isGltfPath(const std::filesystem::path& path);

// Cuelga el contenido de la escena 0 (o de los nodos sin padre) bajo `root`; anade los materiales
// al final de `materials` (los indices de malla ya salen desplazados).
bool importGltf(const std::filesystem::path& path, SceneNode& root, std::vector<Material>& materials,
                GltfReport& report, std::string* error);

// Escribe un .glb con las mallas de `roots` (y sus descendientes).
bool exportGlb(const std::filesystem::path& path, const std::vector<const SceneNode*>& roots,
               const std::vector<Material>& materials, GltfReport& report, std::string* error);

} // namespace iron3d
