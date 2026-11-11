#pragma once

// Imagen final de un canal de material para el visor (2026-11-11): la
// textura del canal pasada por su arbol de shaders (Filtro, Colorizador,
// Capa, Degradado, Color) y mezclada con el color y el brillo del canal
// segun su Modo y Fuerza de Mezcla. Si no hay nada que hacer, es la imagen
// tal cual (mismo camino de siempre, sin coste extra).

#include "Material.h"
#include "TextureImage.h"

#include <string>

namespace iron3d {

// Arbol que manda en el canal: el importado si su primera Imagen sigue siendo
// la textura del canal; si la textura se cambio a mano en Iron3D, nullptr
// (vale la imagen nueva sola).
const ShaderNode* effectiveShader(const Material& m, TexSlot s);

// true si el canal tiene textura (o un shader que genera imagen sin ella).
bool channelHasTexture(const Material& m, TexSlot s);

// Clave para la cache de texturas del visor: cambia cuando cambia cualquier
// cosa que afecte a la imagen. Vacia = el canal no tiene textura.
std::string channelTextureKey(const Material& m, TexSlot s);

// Carga la imagen del canal (horneada si hace falta). false si no hay
// textura o no se encuentra ninguna imagen del arbol.
bool loadChannelTexture(const Material& m, TexSlot s, TextureImage& out);

} // namespace iron3d
