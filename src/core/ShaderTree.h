#pragma once

// Arbol de shaders de un canal de material (2026-11-11). Lo que C4D cuelga
// del campo "Textura" de un canal: una Imagen sola, o un Filtro / Colorizador
// / Capa / Degradado / Color que envuelven a otros shaders. Se guarda tal cual
// (para editarlo y para que no se pierda al guardar el .iron) y el visor lo
// hornea a una imagen con bakeChannel: asi Vulkan, OpenGL y las miniaturas
// ven lo mismo sin tocar sus shaders de GPU.
//
// Implementacion propia: los parametros salen de los .h publicos de
// parametros del SDK (nombres e ids), la matematica es nuestra -- no es el
// resultado exacto de C4D, es una aproximacion fiel para el visor.

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace iron3d {

struct GradientKnot {
    glm::vec3 color{1.0f}; // color * brillo del nudo
    float pos = 0.0f;
    float bias = 0.5f;
    int interpolation = 2; // 0 cubica, 2 suave, 3 lineal (como C4D)
};

enum class ShaderKind {
    None = 0,  // canal sin shader
    Bitmap,    // Imagen
    Color,     // Color
    Filter,    // Filtro
    Colorizer, // Colorizador
    Layer,     // Capa
    Gradient,  // Degradado
    Other,     // procedural u otro que Iron3D no evalua (Ruido, Fresnel...)
};
const char* shaderKindName(ShaderKind k); // en espanol, para la UI

// Una capa del shader Capa (la lista va de arriba, indice 0, a abajo; comprobado
// con modelos reales: una capa Transformar en el 0 deforma la imagen de debajo). `type` = LayerType de C4D (1 carpeta, 2 shader,
// 3 brillo/contraste, 4 HSL, 5 posterizar, 6 colorizar, 7 limitar, 8 recortar,
// 9 distorsion, 10 transformar). `child` = indice en ShaderNode::children
// (capas de tipo shader) o -1.
struct ShaderLayer {
    int type = 2;
    bool active = true;
    int mode = 1;          // BlendMode de C4D (1 Normal, 2 Multiplicar, ... 11 Sumar, 12 Restar...)
    float blend = 1.0f;    // opacidad
    int flags = 0;         // 1 invertir
    int child = -1;
    float a = 0.0f, b = 0.0f, c = 0.0f; // parametros del efecto (brillo/contraste/gamma, H/S/L, niveles...)
    int colorize = 0;      // HSL: colorear; Colorizar: entrada
    bool cycle = false;
    std::vector<GradientKnot> gradient;
};

struct ShaderNode {
    ShaderKind kind = ShaderKind::None;
    int c4dType = 0;       // id del shader en C4D (5833 Imagen, 1011128 Filtro...)
    std::string name;      // el nombre que tenia en C4D
    // Imagen
    std::string path;
    int sampling = 6;      // Muestreo (BITMAPSHADER_INTERPOLATION: 6 = MIP)
    // Color
    glm::vec3 color{1.0f};
    float brightness = 1.0f;
    // Filtro
    float hue = 0.0f, saturation = 0.0f, lightness = 0.0f; // tono en grados; el resto -1..1
    bool colorize = false;
    float fBrightness = 0.0f, contrast = 0.0f, gamma = 1.0f;
    bool clipping = false;
    float lowClip = 0.0f, highClip = 1.0f;
    // Colorizador / Degradado
    int input = 2001;      // SLA_COLORIZER_INPUT_* (2001 luminancia)
    bool cycle = false;
    int gradientType = 2000; // SLA_GRADIENT_TYPE_* (2000 2D-U)
    float angle = 0.0f;    // radianes
    std::vector<GradientKnot> gradient;
    // Capa
    std::vector<ShaderLayer> layers; // de arriba (indice 0) a abajo, como la lista de C4D

    std::vector<ShaderNode> children;

    bool empty() const { return kind == ShaderKind::None; }
    // Primera Imagen del arbol (la que se ensena como "la textura" del canal).
    const ShaderNode* firstBitmap() const;
    ShaderNode* firstBitmap();
    // true si el arbol es una Imagen sola (nada que hornear).
    bool isPlainBitmap() const { return kind == ShaderKind::Bitmap; }
    // Algo que Iron3D no sabe evaluar (para avisar en la UI).
    bool hasUnsupported() const;
    // Llama a f(path) con cada ruta de imagen del arbol (para resolver/extraer).
    template <class F> void forEachPath(F&& f) {
        if (kind == ShaderKind::Bitmap) f(path);
        for (ShaderNode& c : children) c.forEachPath(f);
    }
};

// Texto de una linea para el .iron (y su lectura). decode devuelve false si
// el texto no se entiende (deja `out` vacio).
std::string encodeShaderTree(const ShaderNode& n);
bool decodeShaderTree(const std::string& text, ShaderNode& out);

} // namespace iron3d
