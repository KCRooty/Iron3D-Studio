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

// "Renderizar en Visor de Imagenes" (Mayus+R): la escena entera (visibilidad
// de RENDER, no la del visor) vista desde una camara concreta, con el mismo
// sombreado que las miniaturas. `proj` sin la inversion Y de Vulkan.
// `supersample` = antialiasing NxN (1-4). Fondo en degradado vertical (lineal).
Thumbnail renderCameraImage(const SceneNode& root, const std::vector<Material>& materials, const glm::mat4& view,
                            const glm::mat4& proj, int width, int height, int supersample,
                            const glm::vec3& bgTop, const glm::vec3& bgBottom);

// Efectos de "estudio" del render por CPU (2026-11-24), pensados para logos de cristal y plastico brillante:
//  - studio: reflejos de un entorno de estudio (cielo claro, suelo oscuro con horizonte nitido, dos cajas de luz)
//    con Fresnel; y Transparencia del material (los triangulos transparentes se ordenan de lejos a cerca y se
//    mezclan con lo que hay detras).
//  - shadow: sombra suave en el suelo bajo la escena, en espacio de pantalla.
struct RenderFx {
    bool studio = false;
    bool shadow = false;
};
Thumbnail renderCameraImage(const SceneNode& root, const std::vector<Material>& materials, const glm::mat4& view,
                            const glm::mat4& proj, int width, int height, int supersample,
                            const glm::vec3& bgTop, const glm::vec3& bgBottom, const RenderFx& fx);

} // namespace iron3d
