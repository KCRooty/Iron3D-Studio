#pragma once

// Formato .iron (Fase 2c, AGENTS.md Sec6: "Formato .iron versionado desde el
// primer commit"): texto plano propio, sin dependencias nuevas -- un arbol de
// nodos NODE { ... } con atributos clave=valor, indentado solo por legibilidad
// (el parser no depende de la indentacion). Limitacion conocida y aceptada
// para esta fase: el nombre de un nodo (o de un material) no puede contener
// el caracter `"` (sin escapado todavia).
//
// Fase 5 (materiales): un bloque MATERIALS { MATERIAL ... } opcional antes
// del NODE raiz, y cada NODE lleva ahora `materialIndex=N` (-1 si no tenia
// ese atributo, para que los .iron viejos sigan cargando igual). El bloque
// es opcional en la LECTURA por lo mismo -- un archivo sin el simplemente
// carga 0 materiales.

#include "SceneNode.h"
#include "Material.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iron3d {

// Version en memoria (sin tocar disco): la reutiliza SceneHistory (Fase 2d)
// para los snapshots de deshacer/rehacer -- clonar el arbol entero (y la
// lista de materiales) a texto en cada paso es mas simple que un sistema de
// comandos/deltas, y para el tamano de escena de este proyecto el coste es
// insignificante. Esto tambien hace que deshacer/rehacer cubra sin esfuerzo
// extra tanto la asignacion de material de un nodo como la propia lista de
// materiales (anadir/borrar/editar un material es parte del mismo snapshot).
std::string serializeScene(const SceneNode& root, const std::vector<Material>& materials);
// Misma garantia que loadScene: si `text` no parsea entero, ni `root` ni
// `materials` se tocan.
bool deserializeScene(const std::string& text, SceneNode& root, std::vector<Material>& materials,
                       std::string* outError = nullptr);

bool saveScene(const std::filesystem::path& path, const SceneNode& root,
               const std::vector<Material>& materials, std::string* outError = nullptr);

// Si el archivo no parsea entero, ni `root` ni `materials` se tocan (se
// parsea primero en un arbol/lista temporal) y se devuelve false con el
// motivo en `outError`.
bool loadScene(const std::filesystem::path& path, SceneNode& root, std::vector<Material>& materials,
               std::string* outError = nullptr);

} // namespace iron3d
