#pragma once

// Miniaturas de modelos para las librerias de preestablecidos (.lib4d), como
// las que renderiza C4D al guardar un preestablecido. Rasterizador propio por
// CPU: no necesita la GPU ni el renderer activo, asi que funciona igual desde
// la UI, desde herramientas de consola y en las pruebas (Windows y Linux).
//
// Vista en perspectiva desde arriba a la derecha (como la vista por defecto
// de C4D), encuadrando todo lo visible; caras con el color de su material,
// luz principal + relleno + ambiente, y antialiasing 2x2.

#include "Material.h"
#include "SceneNode.h"

#include <cstdint>
#include <vector>

namespace iron3d {

struct Thumbnail {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // opaca (alfa 255), filas de arriba abajo
    int triangles = 0;         // triangulos dibujados (0 = no habia nada visible)
};

// Renderiza `roots` con sus hijos (lo oculto en el visor no sale). Cada raiz
// se coloca con su propia transformacion local, como al exportarla.
Thumbnail renderThumbnail(const std::vector<const SceneNode*>& roots, const std::vector<Material>& materials,
                          int size = 256);

} // namespace iron3d
