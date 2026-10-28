#pragma once

// Formato .iron (Fase 2c, AGENTS.md Sec6: "Formato .iron versionado desde el
// primer commit"): texto plano propio, sin dependencias nuevas -- un arbol de
// nodos NODE { ... } con atributos clave=valor, indentado solo por legibilidad
// (el parser no depende de la indentacion). Limitacion conocida y aceptada
// para esta fase: el nombre de un nodo no puede contener el caracter `"`
// (sin escapado todavia).

#include "SceneNode.h"

#include <filesystem>
#include <string>

namespace iron3d {

// Version en memoria (sin tocar disco): la reutiliza SceneHistory (Fase 2d)
// para los snapshots de deshacer/rehacer -- clonar el arbol entero a texto
// en cada paso es mas simple que un sistema de comandos/deltas, y para el
// tamano de escena de este proyecto el coste es insignificante.
std::string serializeScene(const SceneNode& root);
// Misma garantia que loadScene: si `text` no parsea entero, `root` no se toca.
bool deserializeScene(const std::string& text, SceneNode& root, std::string* outError = nullptr);

bool saveScene(const std::filesystem::path& path, const SceneNode& root, std::string* outError = nullptr);

// Si el archivo no parsea entero, `root` NO se toca (se parsea primero en un
// arbol temporal) y se devuelve false con el motivo en `outError`.
bool loadScene(const std::filesystem::path& path, SceneNode& root, std::string* outError = nullptr);

} // namespace iron3d
