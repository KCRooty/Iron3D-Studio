#pragma once

// Alembic (.abc, formato Ogawa) (2026-11-25): importar jerarquia de Xform, PolyMesh y SubD (primer
// fotograma, con normales y UV si las trae) y exportar la escena como Xform + PolyMesh estaticos.
// Sin HDF5 (los .abc antiguos de HDF5 no abren), sin animacion de vertices ni camaras todavia.
// Alembic guarda las caras en sentido horario (convencion RenderMan): se invierte al leer y al escribir.

#include "core/Material.h"
#include "core/SceneNode.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct AbcReport {
    int nodes = 0;
    int meshes = 0;
    long long points = 0;
    long long polygons = 0;   // triangulos
    int materials = 0;
    std::vector<std::string> warnings;
};

bool isAbcPath(const std::filesystem::path& path);

bool importAbc(const std::filesystem::path& path, SceneNode& root, std::vector<Material>& materials,
               AbcReport& report, std::string* error);

bool exportAbc(const std::filesystem::path& path, const std::vector<const SceneNode*>& roots,
               const std::vector<Material>& materials, AbcReport& report, std::string* error);

} // namespace iron3d
