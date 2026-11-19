#pragma once

// Importar y exportar Wavefront OBJ (+ MTL) -- formato abierto y sencillo, el mas
// comun para pasar mallas entre programas (2026-11-19). Sin dependencias:
// funciona igual en Windows y Linux, a diferencia del importador de .c4d.
//
// Importar: un nodo con malla por cada `o`/`g`; poligonos de n lados (se guardan
// como tales en MeshData::faceTris); UV (v se invierte: en C4D/Iron3D v=0 es arriba);
// normales del archivo o suaves calculadas; materiales del .mtl (Kd, Ks aproximado,
// map_Kd) como Material con la textura de Color.
// Exportar: la geometria con su transformacion de mundo aplicada, un `o` por nodo
// con malla (las primitivas Cubo/Esfera/Plano se exportan por su malla), y un .mtl
// al lado con el color y la textura de Color de cada material.

#include "core/Material.h"
#include "core/SceneNode.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct ObjReport {
    int objects = 0;
    long long points = 0;
    long long polygons = 0;
    int materials = 0;
    std::vector<std::string> warnings;
};

bool isObjPath(const std::filesystem::path& path);

// Anade los objetos del OBJ como hijos de `root` y los materiales nuevos al final de
// `materials` (los indices de los nodos ya cuentan con los que habia).
bool importObj(const std::filesystem::path& path, SceneNode& root, std::vector<Material>& materials, ObjReport& report,
               std::string* error);

// Escribe `roots` (y sus descendientes) en `path` y el .mtl con el mismo nombre.
bool exportObj(const std::filesystem::path& path, const std::vector<const SceneNode*>& roots,
               const std::vector<Material>& materials, ObjReport& report, std::string* error);

} // namespace iron3d
