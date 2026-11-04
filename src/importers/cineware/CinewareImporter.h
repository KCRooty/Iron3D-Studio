#pragma once

// Importador de escenas de Cinema 4D (.c4d) -- interfaz propia de Iron3D.
//
// La implementacion real (CinewareImporter.cpp) usa el Cineware SDK de MAXON
// y solo se compila si se configura CMake con -DIRON3D_CINEWARE_SDK=<ruta al
// SDK> (ver AGENTS.md: el SDK nunca entra en el repositorio). Sin el SDK --
// el CI, y siempre en Linux, donde Cineware no tiene librerias -- se compila
// CinewareImporterStub.cpp: cinewareAvailable() devuelve false e importC4D()
// explica por que no se puede.

#include "core/Material.h"
#include "core/SceneNode.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

struct C4DImportReport {
    std::string fileName;
    int objects = 0;            // nodos creados
    int meshes = 0;             // objetos con malla (poligonales + reconstruidos + generadores con cache)
    int primitivesRebuilt = 0;  // primitivas de C4D reconstruidas desde sus parametros
    int generatorsFromCache = 0;// generadores importados con la geometria que guardo C4D
    int lights = 0;
    int materials = 0;
    long long points = 0;
    long long polygons = 0;
    // Lo que no ha llegado tal cual (agrupado: "texto (N)"), para que el
    // usuario sepa exactamente que revisar.
    std::vector<std::string> warnings;
};

// true si este ejecutable lleva el importador de Cineware.
bool cinewareAvailable();

bool isC4DPath(const std::filesystem::path& path);

// Importa `file` como hijos nuevos de `parent`; los materiales del archivo se
// anaden al final de `materials`. Si falla, no toca ni `parent` ni
// `materials` y devuelve false con el motivo en `outError`.
bool importC4D(const std::filesystem::path& file, SceneNode& parent, std::vector<Material>& materials,
               C4DImportReport& report, std::string* outError = nullptr);

} // namespace iron3d
