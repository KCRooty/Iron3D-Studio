#pragma once

// Material basico (Fase 5, Documento Maestro: iluminacion + materiales tras
// cerrar el bloque "todo junto" de render/instalador/UI). Sin texturas/PBR
// todavia -- solo los coeficientes de un Phong clasico (difuso + especular +
// brillo), que es lo minimo para que la Luz de la escena (SceneNode con
// primitive=Light) ilumine algo de verdad en vez de ser solo un icono.
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
    glm::vec3 diffuse{0.7f, 0.7f, 0.7f};
    float specular = 0.3f;    // intensidad especular, 0..1
    float shininess = 32.0f;  // exponente Phong -- mas alto, brillo mas concentrado

    // Gris neutro usado cuando un nodo no tiene material asignado
    // (materialIndex == -1) o cuando el indice ya no es valido (p.ej. tras
    // borrar un material sin que el nodo se reasignara a tiempo).
    static const Material& defaultMaterial();
};

// Busqueda segura por indice: devuelve defaultMaterial() si `index` es -1 o
// esta fuera de rango, en vez de que cada llamador tenga que comprobarlo.
const Material& materialOrDefault(const std::vector<Material>& materials, int index);

} // namespace iron3d
