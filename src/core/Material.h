#pragma once

// Material basico (Fase 5, Documento Maestro: iluminacion + materiales tras
// cerrar el bloque "todo junto" de render/instalador/UI).
//
// PBR metallic/roughness (2026-10-30, ver ROADMAP_RENDER.md fase 1) en vez
// del Phong clasico original -- el estandar real de cualquier renderer
// moderno (Redshift incluido). Por decision de Core, esta fase es SOLO
// Vulkan: el backend OpenGL se queda en su Phong de siempre (ver
// OpenGLRenderer.cpp, mapea estos mismos campos a specular/shininess
// aproximados solo para que siga compilando y se vea razonable, no es un
// segundo PBR). El formato de escena .iron tambien cambia sin migracion
// (decision de Core: proyecto en fase temprana, no hay escenas guardadas
// que proteger).
//
// Diseno propio, inspirado en el DESACOPLE real de C4D (BaseMaterial vive en
// una lista global del documento, no dentro del objeto; el objeto solo
// guarda que material usa -- ver TextureTag en la documentacion oficial,
// notas en V:\ObsidianVault\14-Iron3D-CoalMix\sdk_docs_maxon_2027.md). Aqui
// la "lista global" es el std::vector<Material> que vive en main.cpp y se
// pasa por referencia a quien lo necesite (UI, render, serializador) --
// mismo patron que ya usan SceneHistory/Preferences, sin una clase
// "MaterialLibrary" de mas.

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace iron3d {

struct Material {
    std::string name = "Material";
    glm::vec3 baseColor{0.7f, 0.7f, 0.7f}; // albedo (dielectrico) o color de reflejo (metal)
    float metallic = 0.0f;                 // 0 = dielectrico, 1 = metal puro
    float roughness = 0.5f;                // 0 = espejo, 1 = totalmente mate

    // Gris neutro usado cuando un nodo no tiene material asignado
    // (materialIndex == -1) o cuando el indice ya no es valido (p.ej. tras
    // borrar un material sin que el nodo se reasignara a tiempo).
    static const Material& defaultMaterial();
};

// Busqueda segura por indice: devuelve defaultMaterial() si `index` es -1 o
// esta fuera de rango, en vez de que cada llamador tenga que comprobarlo.
const Material& materialOrDefault(const std::vector<Material>& materials, int index);

} // namespace iron3d
