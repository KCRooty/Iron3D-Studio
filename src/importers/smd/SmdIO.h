#pragma once

// Exportar a Valve SMD (Source Engine, 2026-11-20): malla de referencia
// estatica para studiomdl (un hueso "root", una pose, los triangulos con su
// material). Sin dependencias, igual en Windows y Linux.
//
// Conversion de ejes: Iron3D es Y arriba (1 unidad = 200 cm); Source es Z arriba. Se gira
// (x, y, z) -> (x, -z, y), que es un giro propio (no cambia el sentido de
// giro de las caras), y se escala `unitsPerCm` (por defecto 0,525: 1 unidad de
// Source ~ 1,905 cm). El orden de los vertices de cada triangulo se invierte
// porque studiomdl espera las caras frontales en sentido horario.
// Material: el nombre del archivo de la textura de Color (sin carpeta ni
// extension, en minusculas) o, si no hay, el nombre del material; ese es el
// nombre del .vmt que luego hay que crear.
//
// No exporta esqueleto ni pesos (la Piel de Iron3D no se convierte todavia)
// ni animacion: solo geometria, normales y UV.

#include "core/Material.h"
#include "core/SceneNode.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct SmdReport {
    int objects = 0;
    long long triangles = 0;
    std::vector<std::string> materials;  // nombres usados en el archivo
    std::vector<std::string> warnings;
};

// Nombre de material de Source a partir de un Material de Iron3D.
std::string smdMaterialName(const Material& m);

bool exportSmd(const std::filesystem::path& path, const std::vector<const SceneNode*>& roots,
               const std::vector<Material>& materials, SmdReport& report, std::string* error,
               float unitsPerCm = 0.525f);

} // namespace iron3d
