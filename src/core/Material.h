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

// Canales con textura, como los del Material estandar de C4D (2026-11-07).
enum class TexSlot { Color = 0, Roughness, Normal, Bump, Alpha, Emission, Transparency, Count };
constexpr int kTexSlotCount = (int)TexSlot::Count;
const char* texSlotName(TexSlot slot); // en espanol, para la UI
const char* texSlotCode(TexSlot slot); // para el .iron

// Una capa de Reflectancia de C4D tal cual (para que importar y volver a
// exportar un .c4d no pierda nada). El render usa los campos PBR de Material,
// que se deducen de estas capas al importar (pbrFromLayers).
struct ReflectLayer {
    std::string name;
    int distribution = 3;     // REFLECTION_DISTRIBUTION_* de C4D (3 = GGX)
    int fresnel = 0;          // REFLECTION_FRESNEL_* (0 nada, 1 dielectrico, 2 conductor)
    int fresnelPreset = 0;    // preset de IOR (0 = personalizado)
    float strength = 1.0f;    // Reflejo
    float specular = 0.2f;    // Especular
    float roughness = 0.1f;
    float ior = 1.5f;
    glm::vec3 color{1.0f};    // color de la capa * brillo
    bool transparencyLayer = false; // capa "Transparencia" que C4D gestiona sola
    std::string roughnessTex, colorTex;
};

struct Material {
    std::string name = "Material";
    glm::vec3 baseColor{0.7f, 0.7f, 0.7f}; // albedo (dielectrico) o color de reflejo (metal)
    float metallic = 0.0f;                 // 0 = dielectrico, 1 = metal puro
    float roughness = 0.5f;                // 0 = espejo, 1 = totalmente mate

    // Canales del Material de C4D (2026-11-07). Cero / vacio = canal apagado.
    glm::vec3 emission{0.0f};      // Luminancia (color * brillo)
    glm::vec3 transparency{0.0f};  // Transparencia (color * brillo); 0 = opaco
    float ior = 1.0f;              // indice de refraccion de la Transparencia
    float normalStrength = 1.0f;   // Normal (con textura)
    float bumpStrength = 0.2f;     // Relieve (con textura); 0.2 = 20 %, el de C4D
    // Ruta de la textura de cada canal tal como se guarda (relativa o
    // absoluta). Vacia = sin textura. La ruta ya resuelta para cargarla se
    // calcula con resolveTexturePath.
    std::string textures[kTexSlotCount];
    // Carpeta desde la que se resuelven las rutas relativas (la del .c4d
    // importado). Se guarda en el .iron para que la textura se siga encontrando.
    std::string texBaseDir;
    // Capas de Reflectancia originales (vacio = se generan desde los campos PBR).
    std::vector<ReflectLayer> layers;

    const std::string& texture(TexSlot s) const { return textures[(int)s]; }
    std::string& texture(TexSlot s) { return textures[(int)s]; }

    // Gris neutro usado cuando un nodo no tiene material asignado
    // (materialIndex == -1) o cuando el indice ya no es valido (p.ej. tras
    // borrar un material sin que el nodo se reasignara a tiempo).
    static const Material& defaultMaterial();
};

// Busqueda segura por indice: devuelve defaultMaterial() si `index` es -1 o
// esta fuera de rango, en vez de que cada llamador tenga que comprobarlo.
const Material& materialOrDefault(const std::vector<Material>& materials, int index);

// Campos PBR (color, metal, rugosidad) que salen de las capas de Reflectancia
// de C4D: la primera capa que refleja manda. `colorOn` = canal Color activo.
// Devuelve false si ninguna capa aporta reflejo (deja los campos como estan).
bool pbrFromLayers(const std::vector<ReflectLayer>& layers, bool colorOn, glm::vec3& baseColor, float& metallic,
                   float& roughness);
// Capa que representa los campos PBR de `m` (la que escribe el exportador
// cuando no hay capas originales o se han editado los campos PBR).
ReflectLayer layerFromPbr(const Material& m);
// Color del canal Color de C4D que acompana a layerFromPbr (negro en metal).
glm::vec3 colorChannelFromPbr(const Material& m);
// true si las capas originales siguen describiendo los campos PBR de `m`
// (no se han tocado desde que se importaron): se pueden exportar tal cual.
bool layersMatchPbr(const Material& m);

// Ruta de una textura lista para abrir: absoluta si existe; si no, junto a
// `baseDir`, en `baseDir/tex` y en `baseDir/..` (lo que hace C4D: la carpeta
// del documento y su subcarpeta tex). Vacia si no se encuentra.
std::string resolveTexturePath(const std::string& path, const std::string& baseDir);

} // namespace iron3d
