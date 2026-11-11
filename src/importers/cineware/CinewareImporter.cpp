// Importador de .c4d con el Cineware SDK de MAXON (ver CinewareImporter.h).
// Solo se compila con -DIRON3D_CINEWARE_SDK=<ruta>; el SDK no esta en el
// repositorio. Codigo propio de Iron3D escrito sobre la API publica del SDK
// (ningun ejemplo ni documentacion de MAXON copiado aqui).
//
// Que se importa:
// - Jerarquia, nombres, posicion/rotacion/escala (espejo en Z y cm ->
//   unidades, ver c4d_compat/C4DGeometry.h) y visibilidad de visor/render.
// - Objetos poligonales (con la deformacion aplicada si C4D la guardo).
// - Generadores y primitivas con la geometria que C4D guarda en el archivo
//   cuando esta activada "Guardar polígonos para Melange/Cineware"
//   (Preferencias > Archivos). Si no la hay, las primitivas basicas se
//   reconstruyen desde sus parametros y el resto llega como Nulo con aviso.
// - Materiales estandar: Color, Luminancia, Transparencia (con IOR), Normal,
//   Relieve, Alfa y todas las capas de Reflectancia tal cual; las texturas de
//   imagen de cada canal (ruta como esta en el archivo, resuelta junto al
//   .c4d o en tex/). Asignados por etiqueta de textura (con su proyeccion,
//   desplazamiento y longitud), heredada por los hijos como en C4D.
// - UV de la etiqueta UVW.
// - Luces (tipo, color, intensidad, angulos, sombra, decaimiento, area) y
//   Camaras (proyeccion, focal, sensor, zoom y etiqueta Objetivo).
// - Posicion/Rotacion/Escala con los numeros HPB exactos de C4D (sin
//   transformacion congelada) y sus pistas de animacion.
// - Suavizado Phong segun la etiqueta Phong de cada objeto.

#include "CinewareImporter.h"
#include "importers/c4d_compat/C4DGeometry.h"
#include "core/Paths.h"
#include "core/Material.h"

#include "c4d.h"
#include "c4d_selectiontag.h"
#include "blendlayers.h"
#include "c4d_customdatatype/blenddatatype.h"
#include "c4d_customdatatype/customgui_gradient.h"
#include "default_alien_overloads.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <unordered_map>

// Cineware busca esta funcion en el espacio de nombres global. El ID se
// escribe en los .c4d que exporta Iron3D (CinewareExporter.cpp). 1000001 es
// PROVISIONAL: hay que cambiarlo por un ID registrado por Static Forge en
// developers.maxon.net antes de publicar una version que exporte .c4d.
void GetWriterInfo(cineware::Int32& id, cineware::String& appname) {
    id = 1000001; // provisional (pendiente de registrar)
    appname = "Iron3D Studio";
}

namespace iron3d {

namespace {

using namespace cineware;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultPhongRad = 80.0 * kPi / 180.0; // angulo por defecto de la etiqueta Phong

struct Ctx {
    C4DImportReport* report = nullptr;
    std::vector<iron3d::Material> materials; // nuevos, se anaden al final si todo sale bien
    int materialBase = 0;                    // indice que tendra materials[0] en la lista de la escena
    std::unordered_map<BaseMaterial*, int> materialIndex;
    std::map<std::string, int> warnings;
    std::string docDir;                      // carpeta del .c4d (texturas relativas)
    Float fps = 30.0;

    void warn(const std::string& text) { ++warnings[text]; }
};

std::string utf8(const cineware::String& s) {
    Char* c = s.GetCStringCopy(STRINGENCODING_UTF8);
    std::string out = c ? c : "";
    DeleteMem(c);
    return out;
}

glm::dvec3 vec(const cineware::Vector& v) { return {v.x, v.y, v.z}; }

// Matriz de C4D como dmat4 (columnas v1, v2, v3, off), en el espacio de C4D.
glm::dmat4 mat(const cineware::Matrix& m) {
    return glm::dmat4(glm::dvec4(vec(m.v1), 0.0), glm::dvec4(vec(m.v2), 0.0), glm::dvec4(vec(m.v3), 0.0),
                      glm::dvec4(vec(m.off), 1.0));
}

VisMode visFrom(Int32 mode) {
    if (mode == MODE_ON) return VisMode::On;
    if (mode == MODE_OFF) return VisMode::Off;
    return VisMode::Default;
}

std::string typeLabel(BaseObject* op) {
    switch (op->GetType()) {
        case Osds: return "Subdivision Surface";
        case Osymmetry: return "Simetría";
        case Oextrude: return "Extrusión";
        case Olathe: return "Torno";
        case Oloft: return "Forro";
        case Osweep: return "Recorrido";
        case Oarray: return "Matriz";
        case Oboole: return "Booleano";
        case Oinstance: return "Instancia";
        case Ometaball: return "Metaball";
        case Ocamera: return "Cámara";
        case Osky: return "Cielo";
        case Ofloor: return "Suelo";
        case Oenvironment: return "Entorno";
        case Obackground: return "Fondo";
        case Oforeground: return "Primer plano";
        case Ostage: return "Escenario";
        case Ospline: return "Spline";
        case Ocapsule: return "Cápsula";
        case Otube: return "Tubo";
        default: return "tipo " + std::to_string((int)op->GetType());
    }
}

// Parametros por GetParameter (los objetos y shaders con NodeData propio -- Luz,
// Camara, shaders -- no los tienen en el contenedor); si no, el contenedor.
GeData paramData(BaseList2D* op, Int32 id) {
    GeData d;
    if (!op->GetParameter(id, d)) d = GeData();
    return d;
}
Int32 paramInt(BaseList2D* op, Int32 id, Int32 def) {
    const GeData d = paramData(op, id);
    if (d.GetType() == DA_LONG) return d.GetInt32();
    return op->GetDataInstance()->GetInt32(id, def);
}
Bool paramBool(BaseList2D* op, Int32 id, Bool def) {
    const GeData d = paramData(op, id);
    if (d.GetType() == DA_LONG) return d.GetInt32() != 0;
    return op->GetDataInstance()->GetBool(id, def);
}
Float paramFloat(BaseList2D* op, Int32 id, Float def) {
    const GeData d = paramData(op, id);
    if (d.GetType() == DA_REAL) return d.GetFloat();
    return op->GetDataInstance()->GetFloat(id, def);
}
cineware::Vector paramVector(BaseList2D* op, Int32 id, const cineware::Vector& def) {
    const GeData d = paramData(op, id);
    if (d.GetType() == DA_VECTOR) return d.GetVector();
    return op->GetDataInstance()->GetVector(id, def);
}

// --- Materiales -----------------------------------------------------------------

// Nudos de un Degradado de C4D (color * brillo de cada nudo).
std::vector<iron3d::GradientKnot> gradientKnots(const GeData& d) {
    std::vector<iron3d::GradientKnot> out;
    if (d.GetType() != CUSTOMDATATYPE_GRADIENT) return out;
    Gradient* g = static_cast<Gradient*>(d.GetCustomDataType(CUSTOMDATATYPE_GRADIENT));
    if (!g) return out;
    for (Int32 i = 0; i < g->GetKnotCount(); ++i) {
        const cineware::GradientKnot k = g->GetKnot(i);
        iron3d::GradientKnot o;
        o.color = glm::vec3((float)(k.col.x * k.brightness), (float)(k.col.y * k.brightness), (float)(k.col.z * k.brightness));
        o.pos = (float)k.pos;
        o.bias = (float)k.bias;
        o.interpolation = k.interpolation;
        out.push_back(o);
    }
    return out;
}

constexpr float kRadToDeg = 57.29577951f;

// Arbol de shaders de un canal tal cual (Imagen, Color, Filtro, Colorizador,
// Capa, Degradado; el resto queda como "Otro" con sus hijos, para no perder
// las imagenes que lleven dentro). Ver core/ShaderTree.h.
iron3d::ShaderNode shaderTree(BaseList2D* link, Ctx& ctx, int depth = 0) {
    iron3d::ShaderNode n;
    if (!link || depth > 12) return n;
    BaseShader* sh = static_cast<BaseShader*>(link);
    const Int32 type = sh->GetType();
    ++ctx.report->shaders[std::to_string((int)type) + " " + utf8(sh->GetName())];
    n.c4dType = (int)type;
    n.name = utf8(sh->GetName());
    auto kids = [&] {
        for (BaseShader* k = sh->GetDown(); k; k = k->GetNext()) n.children.push_back(shaderTree(k, ctx, depth + 1));
    };
    switch (type) {
        case Xbitmap: {
            n.kind = iron3d::ShaderKind::Bitmap;
            GeData d;
            if (sh->GetParameter(BITMAPSHADER_FILENAME, d)) {
                std::string path = utf8(d.GetFilename().GetString());
                // Cinema 4D guarda las rutas relativas como "./tex/a.png": se quita
                // el "./" para que la ruta vuelva igual que se escribio.
                while (path.size() > 2 && path[0] == '.' && (path[1] == '/' || path[1] == (char)92)) path.erase(0, 2);
                n.path = path;
            }
            n.sampling = (int)paramInt(sh, BITMAPSHADER_INTERPOLATION, BITMAPSHADER_INTERPOLATION_MIP);
            break;
        }
        case Xcolor: {
            n.kind = iron3d::ShaderKind::Color;
            const cineware::Vector c = paramVector(sh, COLORSHADER_COLOR, cineware::Vector(1.0, 1.0, 1.0));
            n.color = glm::vec3((float)c.x, (float)c.y, (float)c.z);
            n.brightness = (float)paramFloat(sh, COLORSHADER_BRIGHTNESS, 1.0);
            break;
        }
        case Xfilter:
            n.kind = iron3d::ShaderKind::Filter;
            n.hue = (float)paramFloat(sh, SLA_FILTER_HUE, 0.0) * kRadToDeg;
            n.saturation = (float)paramFloat(sh, SLA_FILTER_SATURATION, 0.0);
            n.lightness = (float)paramFloat(sh, SLA_FILTER_LIGHTNESS, 0.0);
            n.colorize = paramBool(sh, SLA_FILTER_COLORIZE, false);
            n.fBrightness = (float)paramFloat(sh, SLA_FILTER_BRIGHNESS, 0.0);
            n.contrast = (float)paramFloat(sh, SLA_FILTER_CONTRAST, 0.0);
            n.gamma = (float)paramFloat(sh, SLA_FILTER_GAMMA, 1.0);
            n.clipping = paramBool(sh, SLA_FILTER_CLIPPING, false);
            n.lowClip = (float)paramFloat(sh, SLA_FILTER_LOW_CLIP, 0.0);
            n.highClip = (float)paramFloat(sh, SLA_FILTER_HIGH_CLIP, 1.0);
            kids();
            break;
        case Xcolorizer:
            n.kind = iron3d::ShaderKind::Colorizer;
            n.input = (int)paramInt(sh, SLA_COLORIZER_INPUT, SLA_COLORIZER_INPUT_LUMINANCE);
            n.cycle = paramBool(sh, SLA_COLORIZER_CYCLE, false);
            n.gradient = gradientKnots(paramData(sh, SLA_COLORIZER_GRADIENT));
            kids();
            break;
        case Xgradient:
            n.kind = iron3d::ShaderKind::Gradient;
            n.gradientType = (int)paramInt(sh, SLA_GRADIENT_TYPE, SLA_GRADIENT_TYPE_2D_U);
            n.cycle = paramBool(sh, SLA_GRADIENT_CYCLE, false);
            n.angle = (float)paramFloat(sh, SLA_GRADIENT_ANGLE, 0.0);
            n.gradient = gradientKnots(paramData(sh, SLA_GRADIENT_GRADIENT));
            break;
        case Xlayer: {
            n.kind = iron3d::ShaderKind::Layer;
            std::vector<BaseShader*> childPtrs;
            for (BaseShader* k = sh->GetDown(); k; k = k->GetNext()) childPtrs.push_back(k);
            kids();
            const GeData d = paramData(sh, SLA_LAYER_BLEND);
            iBlendDataType* bd = d.GetType() == CUSTOMDATA_BLEND_LIST
                                     ? static_cast<iBlendDataType*>(d.GetCustomDataType(CUSTOMDATA_BLEND_LIST))
                                     : nullptr;
            if (!bd) {
                ctx.warn("Shader Capa sin lista de capas legible: se usa la primera imagen");
                n.kind = iron3d::ShaderKind::Other;
                break;
            }
            int nextShader = 0;
            const Int32 count = bd->m_BlendLayers.GetElementCount();
            for (Int32 i = 0; i < count; ++i) {
                BlendLayer* bl = bd->m_BlendLayers.GetLayer(i);
                if (!bl) continue;
                iron3d::ShaderLayer l;
                l.type = (int)bl->GetType();
                GeData v;
                auto getF = [&](Int32 id, float def) { return bl->GetParameter(id, v) && v.GetType() == DA_REAL ? (float)v.GetFloat() : def; };
                auto getI = [&](Int32 id, int def) { return bl->GetParameter(id, v) && v.GetType() == DA_LONG ? (int)v.GetInt32() : def; };
                l.active = getI(LAYER_S_PARAM_ALL_ACTIVE, 1) != 0;
                l.flags = getI(LAYER_S_PARAM_ALL_FLAGS, 0);
                switch (l.type) {
                    case TypeShader: {
                        l.mode = getI(LAYER_S_PARAM_SHADER_MODE, BlendNormal);
                        l.blend = getF(LAYER_S_PARAM_SHADER_BLEND, 1.0f);
                        // El shader de la capa: por enlace si se puede, si no por orden.
                        BaseList2D* target = bl->GetParameter(LAYER_S_PARAM_SHADER_LINK, v) ? v.GetLink() : nullptr;
                        for (size_t c = 0; c < childPtrs.size(); ++c)
                            if (childPtrs[c] == target) l.child = (int)c;
                        if (l.child < 0 && nextShader < (int)childPtrs.size()) l.child = nextShader;
                        ++nextShader;
                        break;
                    }
                    case TypeFolder:
                        l.mode = getI(LAYER_S_PARAM_FOLDER_MODE, BlendNormal);
                        l.blend = getF(LAYER_S_PARAM_FOLDER_BLEND, 1.0f);
                        ctx.warn("Carpetas dentro del shader Capa: todavía no se evalúan");
                        break;
                    case TypeBrightnessContrast:
                        l.a = getF(LAYER_S_PARAM_BC_BRIGHTNESS, 0.0f);
                        l.b = getF(LAYER_S_PARAM_BC_CONTRAST, 0.0f);
                        l.c = getF(LAYER_S_PARAM_BC_GAMMA, 1.0f);
                        break;
                    case TypeHSL:
                        l.a = getF(LAYER_S_PARAM_HSL_HUE, 0.0f) * kRadToDeg;
                        l.b = getF(LAYER_S_PARAM_HSL_SATURATION, 0.0f);
                        l.c = getF(LAYER_S_PARAM_HSL_LIGHTNESS, 0.0f);
                        l.colorize = getI(LAYER_S_PARAM_HSL_COLORIZE, 0);
                        break;
                    case TypePosterize:
                        l.a = (float)getI(LAYER_S_PARAM_POSTER_LEVELS, 16);
                        l.b = getF(LAYER_S_PARAM_POSTER_WIDTH, 1.0f);
                        break;
                    case TypeColorize:
                        l.colorize = getI(LAYER_S_PARAM_COLORIZE_INPUT, 0);
                        l.cycle = getI(LAYER_S_PARAM_COLORIZE_CYCLE, 0) != 0;
                        if (bl->GetParameter(LAYER_S_PARAM_COLORIZE_GRADIENT, v)) l.gradient = gradientKnots(v);
                        break;
                    case TypeClamp:
                        l.a = getF(LAYER_S_PARAM_CLAMP_LOW_CLIP, 0.0f);
                        l.b = getF(LAYER_S_PARAM_CLAMP_HIGH_CLIP, 1.0f);
                        break;
                    case TypeClip:
                        l.a = getF(LAYER_S_PARAM_CLIP_LOW_CLIP, 0.0f);
                        l.b = getF(LAYER_S_PARAM_CLIP_HIGH_CLIP, 1.0f);
                        break;
                    default: break;
                }
                n.layers.push_back(std::move(l));
            }
            break;
        }
        default:
            n.kind = iron3d::ShaderKind::Other;
            kids();
            break;
    }
    if (depth == 0 && std::getenv("IRON3D_C4D_DEBUG"))
        std::fprintf(stderr, "[c4d] shader: %s\n", iron3d::encodeShaderTree(n).c_str());
    return n;
}

// Ruta de la primera Imagen de un enlace a shader (texturas de las capas de Reflectancia).
std::string bitmapPath(BaseList2D* link, Ctx& ctx) {
    const iron3d::ShaderNode n = shaderTree(link, ctx);
    const iron3d::ShaderNode* b = n.firstBitmap();
    return b ? b->path : std::string();
}

glm::vec3 colorTimes(BaseContainer* bc, Int32 colorId, Int32 brightId, const cineware::Vector& defColor) {
    const cineware::Vector c = bc->GetVector(colorId, defColor);
    const double br = bc->GetFloat(brightId, 1.0);
    return glm::vec3((float)(c.x * br), (float)(c.y * br), (float)(c.z * br));
}

void noteTexture(const std::string& path, Ctx& ctx) {
    if (path.empty()) return;
    ++ctx.report->textures;
    if (iron3d::resolveTexturePath(path, ctx.docDir).empty()) {
        ++ctx.report->texturesMissing;
        ctx.warn("Texturas que no están en disco (ni junto al .c4d ni en tex/): se conserva la ruta");
    }
}

int materialFor(BaseMaterial* m, Ctx& ctx) {
    if (!m) return -1;
    auto it = ctx.materialIndex.find(m);
    if (it != ctx.materialIndex.end()) return it->second;

    iron3d::Material out;
    out.name = utf8(m->GetName());
    out.texBaseDir = ctx.docDir;
    out.roughness = 0.5f;
    out.metallic = 0.0f;
    if (m->GetType() == Mmaterial) {
        BaseContainer* bc = m->GetDataInstance();
        auto vec = [](const cineware::Vector& v) { return glm::vec3((float)v.x, (float)v.y, (float)v.z); };
        // Canal completo: color y brillo por separado, mezcla y arbol de shaders.
        auto channel = [&](iron3d::TexSlot slot, Int32 linkId, Int32 colorId, Int32 brightId, Int32 strengthId,
                           Int32 mixId, const cineware::Vector& defColor) {
            iron3d::ChannelSettings& c = out.channel(slot);
            if (colorId) {
                c.color = vec(bc->GetVector(colorId, defColor));
                c.brightness = (float)bc->GetFloat(brightId, 1.0);
                c.mixStrength = (float)bc->GetFloat(strengthId, 1.0);
                c.mixMode = (int)bc->GetInt32(mixId, MATERIAL_TEXTUREMIXING_NORMAL);
            }
            c.shader = shaderTree(bc->GetLink(linkId), ctx);
            if (const iron3d::ShaderNode* b = c.shader.firstBitmap()) {
                out.texture(slot) = b->path;
                c.sampling = b->sampling;
            }
            if (c.shader.hasUnsupported())
                ctx.warn("Canales con shaders procedurales de C4D (Ruido, Fresnel...): Iron3D no los evalúa (gris o la imagen que llevan dentro)");
            noteTexture(out.texture(slot), ctx);
        };
        const bool colorOn = bc->GetBool(MATERIAL_USE_COLOR, true);
        out.useColor = colorOn;
        out.useLuminance = bc->GetBool(MATERIAL_USE_LUMINANCE, false);
        out.useTransparency = bc->GetBool(MATERIAL_USE_TRANSPARENCY, false);
        out.useNormal = bc->GetBool(MATERIAL_USE_NORMAL, false);
        out.useBump = bc->GetBool(MATERIAL_USE_BUMP, false);
        out.useAlpha = bc->GetBool(MATERIAL_USE_ALPHA, false);
        out.useReflectance = bc->GetBool(MATERIAL_USE_REFLECTION, true);
        out.diffuseModel = (int)bc->GetInt32(MATERIAL_COLOR_MODEL, MATERIAL_COLOR_MODEL_LAMBERTIAN);
        out.diffuseFalloff = (float)bc->GetFloat(MATERIAL_COLOR_DIFFUSEFALLOFF, 0.0);
        out.diffuseLevel = (float)bc->GetFloat(MATERIAL_COLOR_DIFFUSELEVEL, 1.0);
        out.diffuseRoughness = (float)bc->GetFloat(MATERIAL_COLOR_ROUGHNESS, 0.5);
        // Los ajustes se leen aunque el canal este apagado (asi no se pierden al
        // encenderlo); lo que pinta el visor solo sale de los canales activos.
        channel(iron3d::TexSlot::Color, MATERIAL_COLOR_SHADER, MATERIAL_COLOR_COLOR, MATERIAL_COLOR_BRIGHTNESS,
                MATERIAL_COLOR_TEXTURESTRENGTH, MATERIAL_COLOR_TEXTUREMIXING, cineware::Vector(0.8, 0.8, 0.8));
        if (colorOn) {
            out.baseColor = glm::clamp(colorTimes(bc, MATERIAL_COLOR_COLOR, MATERIAL_COLOR_BRIGHTNESS, cineware::Vector(0.8, 0.8, 0.8)),
                                       0.0f, 1.0f);
        } else {
            out.baseColor = glm::vec3(0.0f);
            out.texture(iron3d::TexSlot::Color).clear();
        }
        channel(iron3d::TexSlot::Emission, MATERIAL_LUMINANCE_SHADER, MATERIAL_LUMINANCE_COLOR, MATERIAL_LUMINANCE_BRIGHTNESS,
                MATERIAL_LUMINANCE_TEXTURESTRENGTH, MATERIAL_LUMINANCE_TEXTUREMIXING, cineware::Vector(1.0, 1.0, 1.0));
        if (out.useLuminance)
            out.emission = colorTimes(bc, MATERIAL_LUMINANCE_COLOR, MATERIAL_LUMINANCE_BRIGHTNESS, cineware::Vector(1.0, 1.0, 1.0));
        else
            out.texture(iron3d::TexSlot::Emission).clear();
        channel(iron3d::TexSlot::Transparency, MATERIAL_TRANSPARENCY_SHADER, MATERIAL_TRANSPARENCY_COLOR,
                MATERIAL_TRANSPARENCY_BRIGHTNESS, MATERIAL_TRANSPARENCY_TEXTURESTRENGTH, MATERIAL_TRANSPARENCY_TEXTUREMIXING,
                cineware::Vector(1.0, 1.0, 1.0));
        out.ior = (float)bc->GetFloat(MATERIAL_TRANSPARENCY_REFRACTION, 1.0);
        if (out.useTransparency)
            out.transparency = colorTimes(bc, MATERIAL_TRANSPARENCY_COLOR, MATERIAL_TRANSPARENCY_BRIGHTNESS,
                                          cineware::Vector(1.0, 1.0, 1.0));
        else
            out.texture(iron3d::TexSlot::Transparency).clear();
        channel(iron3d::TexSlot::Normal, MATERIAL_NORMAL_SHADER, 0, 0, 0, 0, cineware::Vector());
        out.normalStrength = (float)bc->GetFloat(MATERIAL_NORMAL_STRENGTH, 1.0);
        if (!out.useNormal) out.texture(iron3d::TexSlot::Normal).clear();
        channel(iron3d::TexSlot::Bump, MATERIAL_BUMP_SHADER, 0, 0, 0, 0, cineware::Vector());
        out.bumpStrength = (float)bc->GetFloat(MATERIAL_BUMP_STRENGTH, 0.2);
        if (!out.useBump) out.texture(iron3d::TexSlot::Bump).clear();
        channel(iron3d::TexSlot::Alpha, MATERIAL_ALPHA_SHADER, 0, 0, 0, 0, cineware::Vector());
        out.alphaColor = vec(bc->GetVector(MATERIAL_ALPHA_COLOR, cineware::Vector()));
        out.alphaDelta = (float)bc->GetFloat(MATERIAL_ALPHA_DELTA, 0.0);
        out.alphaInvert = bc->GetBool(MATERIAL_ALPHA_INVERT, false);
        out.alphaSoft = bc->GetBool(MATERIAL_ALPHA_SOFT, true);
        out.alphaImage = bc->GetBool(MATERIAL_ALPHA_IMAGEALPHA, true);
        if (!out.useAlpha) out.texture(iron3d::TexSlot::Alpha).clear();
        out.reflectionBrightness = (float)bc->GetFloat(REFLECTION_LAYER_GLOBAL_REFLECTION, 1.0);
        out.specularBrightness = (float)bc->GetFloat(REFLECTION_LAYER_GLOBAL_SPECULAR, 1.0);
        out.separatePass = bc->GetBool(REFLECTION_LAYER_GLOBAL_SEPARATE_PASS, false);
        if (out.useReflectance) {
            cineware::Material* mm = static_cast<cineware::Material*>(m);
            const Int32 n = mm->GetReflectionLayerCount();
            for (Int32 i = 0; i < n; ++i) {
                ReflectionLayer* layer = mm->GetReflectionLayerIndex(i);
                if (!layer) continue;
                const Int32 id = layer->GetDataID();
                iron3d::ReflectLayer l;
                l.name = utf8(layer->GetName());
                l.transparencyLayer = (layer->GetFlags() & REFLECTION_FLAG_TRANSPARENCY) != 0;
                l.distribution = bc->GetInt32(id + REFLECTION_LAYER_MAIN_DISTRIBUTION, REFLECTION_DISTRIBUTION_GGX);
                l.fresnel = bc->GetInt32(id + REFLECTION_LAYER_FRESNEL_MODE, REFLECTION_FRESNEL_NONE);
                l.fresnelPreset = bc->GetInt32(id + REFLECTION_LAYER_FRESNEL_PRESET, 0);
                l.strength = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_REFLECTION, 1.0);
                l.specular = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_SPECULAR, 0.2);
                l.roughness = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_ROUGHNESS, 0.1);
                l.ior = (float)bc->GetFloat(id + REFLECTION_LAYER_FRESNEL_VALUE_IOR, 1.5);
                l.color = colorTimes(bc, id + REFLECTION_LAYER_COLOR_COLOR, id + REFLECTION_LAYER_COLOR_BRIGHTNESS,
                                     cineware::Vector(1.0, 1.0, 1.0));
                l.colorRaw = vec(bc->GetVector(id + REFLECTION_LAYER_COLOR_COLOR, cineware::Vector(1.0, 1.0, 1.0)));
                l.colorBrightness = (float)bc->GetFloat(id + REFLECTION_LAYER_COLOR_BRIGHTNESS, 1.0);
                l.colorMixMode = (int)bc->GetInt32(id + REFLECTION_LAYER_COLOR_MIX_MODE, 0);
                l.colorMixStrength = (float)bc->GetFloat(id + REFLECTION_LAYER_COLOR_MIX_STRENGTH, 1.0);
                l.attenuation = (int)bc->GetInt32(id + REFLECTION_LAYER_MAIN_ADDITIVE, REFLECTION_ADDITIVE_MODE_AVG);
                l.blendMode = (int)bc->GetInt32(id + REFLECTION_LAYER_MAIN_BLEND_MODE, 0);
                l.bumpStrength = (float)bc->GetFloat(id + REFLECTION_LAYER_MAIN_VALUE_BUMP, 1.0);
                l.active = (layer->GetFlags() & REFLECTION_FLAG_ACTIVE) != 0;
                l.roughnessTex = bitmapPath(bc->GetLink(id + REFLECTION_LAYER_MAIN_SHADER_ROUGHNESS), ctx);
                l.colorTex = bitmapPath(bc->GetLink(id + REFLECTION_LAYER_COLOR_TEXTURE), ctx);
                noteTexture(l.roughnessTex, ctx);
                noteTexture(l.colorTex, ctx);
                out.layers.push_back(std::move(l));
            }
            if (!iron3d::pbrFromLayers(out.layers, colorOn, out.baseColor, out.metallic, out.roughness)) {
                out.metallic = 0.0f;
                out.roughness = 0.5f;
            }
            for (const iron3d::ReflectLayer& l : out.layers)
                if (!l.transparencyLayer && l.distribution != REFLECTION_DISTRIBUTION_LAMBERTIAN &&
                    l.distribution != REFLECTION_DISTRIBUTION_ORENNAYAR) {
                    out.texture(iron3d::TexSlot::Roughness) = l.roughnessTex;
                    break;
                }
            if (std::getenv("IRON3D_C4D_DEBUG")) {
                const iron3d::ChannelSettings& cc = out.channel(iron3d::TexSlot::Color);
                std::fprintf(stderr,
                             "[c4d] material '%s': %d capas -> metal=%.3f rugosidad=%.3f | color on=%d (%.2f %.2f %.2f) br=%.2f "
                             "mix=%d str=%.2f tex='%s' | lum=%d transp=%d bump=%d alfa=%d normal=%d\n",
                             out.name.c_str(), (int)out.layers.size(), out.metallic, out.roughness, (int)out.useColor,
                             cc.color.r, cc.color.g, cc.color.b, cc.brightness, cc.mixMode, cc.mixStrength,
                             out.texture(iron3d::TexSlot::Color).c_str(), (int)out.useLuminance, (int)out.useTransparency,
                             (int)out.useBump, (int)out.useAlpha, (int)out.useNormal);
                for (const iron3d::ReflectLayer& l : out.layers)
                    std::fprintf(stderr,
                                 "[c4d]    capa '%s' dist=%d fresnel=%d att=%d modo=%d refl=%.2f spec=%.2f rug=%.2f "
                                 "color=(%.2f %.2f %.2f)x%.2f ctex='%s'\n",
                                 l.name.c_str(), l.distribution, l.fresnel, l.attenuation, l.blendMode, l.strength,
                                 l.specular, l.roughness, l.colorRaw.r, l.colorRaw.g, l.colorRaw.b, l.colorBrightness,
                                 l.colorTex.c_str());
            }
        }
    } else {
        ctx.warn("Materiales que no son el material estándar de C4D (p. ej. de otros motores): se importan en gris");
    }
    const int index = ctx.materialBase + (int)ctx.materials.size();
    ctx.materials.push_back(std::move(out));
    ctx.materialIndex.emplace(m, index);
    ++ctx.report->materials;
    return index;
}

TextureTagData textureTagData(BaseTag* t) {
    BaseContainer* bc = t->GetDataInstance();
    TextureTagData d;
    d.projection = bc->GetInt32(TEXTURETAG_PROJECTION, TEXTURETAG_PROJECTION_UVW);
    d.offset = glm::vec2((float)bc->GetFloat(TEXTURETAG_OFFSETX, 0.0), (float)bc->GetFloat(TEXTURETAG_OFFSETY, 0.0));
    d.length = glm::vec2((float)bc->GetFloat(TEXTURETAG_LENGTHX, 1.0), (float)bc->GetFloat(TEXTURETAG_LENGTHY, 1.0));
    d.tile = bc->GetBool(TEXTURETAG_TILE, true);
    d.seamless = bc->GetBool(TEXTURETAG_SEAMLESS, false);
    d.side = bc->GetInt32(TEXTURETAG_SIDE, 0);
    return d;
}

std::string textureTagSelection(BaseTag* t) { return utf8(t->GetDataInstance()->GetString(TEXTURETAG_RESTRICTION)); }

// Material del objeto: la etiqueta de textura SIN restriccion mas a la derecha
// (la que manda en C4D), con su proyeccion y mosaico. Las restringidas a una
// Seleccion de Poligonos van aparte (selectionParts). Sin etiqueta se hereda.
int materialOfObject(BaseObject* op, int inherited, TextureTagData& texTag, Ctx& ctx) {
    int result = inherited;
    int tags = 0;
    for (BaseTag* t = op->GetFirstTag(); t; t = t->GetNext()) {
        if (t->GetType() != Ttexture) continue;
        BaseMaterial* m = static_cast<TextureTag*>(t)->GetMaterial();
        if (!m) continue;
        const TextureTagData d = textureTagData(t);
        if (d.projection != TEXTURETAG_PROJECTION_UVW)
            ctx.warn("Etiquetas de textura con proyección distinta de UVW (Esférica, Cúbica...): se conserva, pero Iron3D "
                     "dibuja con las UV");
        if (!textureTagSelection(t).empty()) continue;
        ++tags;
        result = materialFor(m, ctx);
        texTag = d;
    }
    if (tags > 1) ctx.warn("Objetos con varias etiquetas de textura sin selección: vale la de más a la derecha");
    return result;
}

// Etiquetas de Textura restringidas a una Seleccion de Poligonos (2026-11-11):
// en C4D, cada una pinta solo esos poligonos, y la de mas a la derecha manda
// donde se pisan (una sin restriccion a la derecha las tapa todas). `slot`
// sale con la parte de cada poligono (-1 = el material del objeto). Antes se
// aplicaban a todo el objeto: el traje de Afton salia blanco entero por la
// etiqueta del cuello de la camisa.
void selectionParts(BaseObject* op, Int32 polyCount, std::vector<MaterialPart>& parts, std::vector<int>& slot,
                    Ctx& ctx) {
    parts.clear();
    slot.assign((size_t)std::max<Int32>(polyCount, 0), -1);
    for (BaseTag* t = op->GetFirstTag(); t; t = t->GetNext()) {
        if (t->GetType() != Ttexture) continue;
        BaseMaterial* m = static_cast<TextureTag*>(t)->GetMaterial();
        if (!m) continue;
        const std::string sel = textureTagSelection(t);
        if (sel.empty()) {
            std::fill(slot.begin(), slot.end(), -1);
            continue;
        }
        BaseSelect* bs = nullptr;
        for (BaseTag* st = op->GetFirstTag(); st && !bs; st = st->GetNext())
            if (st->GetType() == Tpolygonselection && utf8(st->GetName()) == sel)
                bs = static_cast<SelectionTag*>(st)->GetBaseSelect();
        if (!bs) {
            ctx.warn("Etiquetas de textura restringidas a una selección que no existe en el objeto: no pintan nada");
            continue;
        }
        MaterialPart part;
        part.materialIndex = materialFor(m, ctx);
        part.texTag = textureTagData(t);
        part.selection = sel;
        const int k = (int)parts.size();
        parts.push_back(part);
        Int32 seg = 0, a = 0, b = 0;
        while (bs->GetRange(seg++, &a, &b))
            for (Int32 i = std::max<Int32>(a, 0); i <= b && i < polyCount; ++i) slot[(size_t)i] = k;
    }
}

// Angulo de suavizado de la etiqueta Phong; -1 = sin etiqueta (caras planas).
double phongAngle(BaseObject* op) {
    BaseTag* t = op->GetTag(Tphong);
    if (!t) return -1.0;
    BaseContainer* bc = t->GetDataInstance();
    if (!bc->GetBool(PHONGTAG_PHONG_ANGLELIMIT, false)) return kPi;
    return bc->GetFloat(PHONGTAG_PHONG_ANGLE, kDefaultPhongRad);
}

// --- Geometria ------------------------------------------------------------------

// Deformador Piel de C4D horneado al importar (2026-11-10): los puntos de una
// malla con Piel estan guardados en la pose del bind; sin evaluar la Piel, una
// cabeza de personaje de .lib4d salia 100 veces mas grande y lejos del cuerpo.
// Piel lineal, la de C4D por defecto: cada punto pasa a mundo con la matriz de
// la geometria en el bind (GetGeomMg), cada articulacion j lo mueve con
// Mg_actual(j) * inversa(Mg_bind(j)) segun su peso, lo que no tiene peso se
// queda donde estaba, y el resultado vuelve al espacio local del objeto. La
// pose es la que C4D guardo en el archivo (sin animacion de las
// articulaciones todavia). Metodo publico (linear blend skinning), datos via
// la API documentada de Cineware (WeightTagData), sin codigo de Maxon.
bool skinnedPoints(PolygonObject* op, std::vector<cineware::Vector>& out) {
    BaseTag* tag = op->GetTag(Tweights);
    if (!tag) return false;
    bool skinOn = false;
    for (BaseObject* ch = op->GetDown(); ch; ch = ch->GetNext())
        if (ch->GetType() == Oskin && ch->GetDeformMode()) skinOn = true;
    if (!skinOn) return false;
    auto* wt = static_cast<WeightTagData*>(tag->GetNodeData());
    BaseDocument* doc = op->GetDocument();
    const cineware::Vector* p = op->GetPointR();
    const Int32 np = op->GetPointCount();
    if (!wt || !doc || !p || np <= 0) return false;

    const cineware::Matrix geomMg = wt->GetGeomMg();
    std::vector<cineware::Vector> bind((size_t)np), acc((size_t)np, cineware::Vector(0.0));
    std::vector<double> wsum((size_t)np, 0.0);
    for (Int32 i = 0; i < np; ++i) bind[(size_t)i] = geomMg * p[i];

    bool any = false;
    const Int32 nj = wt->GetJointCount();
    // Pesos por mapa completo (GetWeightMap): medido 2026-11-10 con William
    // Afton.lib4d, GetWeightCount/GetIndexWeight dejaba casi todos los puntos
    // con suma < 1 y la cabeza seguia lejos; el mapa da suma 1 en todos.
    std::vector<cineware::Float> map((size_t)np);
    for (Int32 j = 0; j < nj; ++j) {
        BaseObject* joint = wt->GetJoint(j, doc);
        if (!joint) continue;
        const cineware::Matrix d = joint->GetMg() * wt->GetJointRestState(j).m_oMi;
        std::fill(map.begin(), map.end(), 0.0);
        wt->GetWeightMap(j, map.data(), np);
        for (Int32 pi = 0; pi < np; ++pi) {
            const cineware::Float w = map[(size_t)pi];
            if (w <= 0.0) continue;
            acc[(size_t)pi] += (d * bind[(size_t)pi]) * w;
            wsum[(size_t)pi] += w;
            any = true;
        }
    }
    if (!any) return false;
    // La parte sin peso no se mueve: se queda en su posicion local del objeto.
    const cineware::Matrix mg = op->GetMg();
    const cineware::Matrix toLocal = ~mg;
    out.resize((size_t)np);
    for (Int32 i = 0; i < np; ++i) {
        const double s = wsum[(size_t)i];
        cineware::Vector g = s > 1.0 ? acc[(size_t)i] * (1.0 / s) : acc[(size_t)i] + (mg * p[i]) * (1.0 - s);
        out[(size_t)i] = toLocal * g;
    }
    return true;
}

// `points`: puntos que sustituyen a los del objeto (la malla con la Piel ya
// aplicada, ver skinnedPoints); nullptr = los del objeto.
void appendPolygons(PolygonObject* po, const glm::dmat4& xform, c4d::PolySoup& out,
                    const std::vector<cineware::Vector>* points = nullptr) {
    const cineware::Vector* p = points ? points->data() : po->GetPointR();
    const CPolygon* f = po->GetPolygonR();
    const Int32 np = points ? (Int32)points->size() : po->GetPointCount(), nf = po->GetPolygonCount();
    if (!p || !f || np <= 0 || nf <= 0) return;
    c4d::PolySoup s;
    s.points.reserve((size_t)np);
    for (Int32 i = 0; i < np; ++i) s.points.push_back(vec(p[i]));
    s.polys.reserve((size_t)nf);
    for (Int32 i = 0; i < nf; ++i) s.polys.push_back({(int)f[i].a, (int)f[i].b, (int)f[i].c, (int)f[i].d});
    // UV de la etiqueta UVW (una por poligono, esquinas a b c d).
    if (BaseTag* t = po->GetTag(Tuvw)) {
        UVWTag* uvw = static_cast<UVWTag*>(t);
        if (uvw->GetDataCount() == nf) {
            ConstUVWHandle h = uvw->GetDataAddressR();
            s.uvs.resize((size_t)nf);
            for (Int32 i = 0; i < nf; ++i) {
                UVWStruct u;
                UVWTag::Get(h, i, u);
                s.uvs[(size_t)i] = {glm::dvec2(u.a.x, u.a.y), glm::dvec2(u.b.x, u.b.y), glm::dvec2(u.c.x, u.c.y),
                                    glm::dvec2(u.d.x, u.d.y)};
            }
        }
    }
    out.append(s, xform);
}

// Objeto de una cache (jerarquia virtual que guarda C4D): primero su cache
// de deformacion, si no su propia cache, si no el objeto mismo; y despues
// sus hijos. Es el recorrido que describe la documentacion de GetCache().
void collectVirtual(BaseObject* op, const glm::dmat4& xform, c4d::PolySoup& out, int depth) {
    if (!op || depth > 64) return;
    if (BaseObject* dc = op->GetDeformCache()) {
        collectVirtual(dc, xform, out, depth + 1);
    } else if (BaseObject* c = op->GetCache()) {
        for (BaseObject* k = c; k; k = k->GetNext()) collectVirtual(k, xform * mat(k->GetMl()), out, depth + 1);
    } else if (!op->GetBit(BIT_CONTROLOBJECT) && op->GetType() == Opolygon) {
        appendPolygons(static_cast<PolygonObject*>(op), xform, out);
    }
    for (BaseObject* ch = op->GetDown(); ch; ch = ch->GetNext())
        collectVirtual(ch, xform * mat(ch->GetMl()), out, depth + 1);
}

// Geometria guardada por C4D para un generador/primitiva (en su espacio local).
bool geometryFromCache(BaseObject* op, c4d::PolySoup& out) {
    BaseObject* c = op->GetDeformCache();
    if (c) {
        collectVirtual(c, glm::dmat4(1.0), out, 0);
    } else if ((c = op->GetCache()) != nullptr) {
        for (BaseObject* k = c; k; k = k->GetNext()) collectVirtual(k, mat(k->GetMl()), out, 0);
    }
    return !out.empty();
}

bool rebuildPrimitive(BaseObject* op, c4d::PolySoup& out, Ctx& ctx) {
    BaseContainer* bc = op->GetDataInstance();
    const c4d::Axis axis = c4d::axisFromC4D(bc->GetInt32(PRIM_AXIS, 2));
    switch (op->GetType()) {
        case Ocube: {
            const cineware::Vector v = bc->GetVector(PRIM_CUBE_LEN, cineware::Vector(200.0, 200.0, 200.0));
            if (bc->GetBool(PRIM_CUBE_DOFILLET, false)) ctx.warn("Cubos con canto redondeado: se importan sin el redondeo");
            out = c4d::makeCube(vec(v));
            return true;
        }
        case Osphere:
            out = c4d::makeSphere(bc->GetFloat(PRIM_SPHERE_RAD, 100.0), bc->GetInt32(PRIM_SPHERE_SUB, 24));
            return true;
        case Oplane:
            out = c4d::makePlane(bc->GetFloat(PRIM_PLANE_WIDTH, 400.0), bc->GetFloat(PRIM_PLANE_HEIGHT, 400.0), axis);
            return true;
        case Ocylinder:
            if (bc->GetBool(PRIM_CYLINDER_FILLET, false)) ctx.warn("Cilindros con canto redondeado: se importan sin el redondeo");
            out = c4d::makeCylinder(bc->GetFloat(PRIM_CYLINDER_RADIUS, 50.0), bc->GetFloat(PRIM_CYLINDER_HEIGHT, 200.0),
                                    bc->GetInt32(PRIM_CYLINDER_SEG, 36), bc->GetBool(PRIM_CYLINDER_CAPS, true), axis);
            return true;
        case Ocone:
            if (bc->GetBool(PRIM_CONE_TOPFILLET, false) || bc->GetBool(PRIM_CONE_BOTFILLET, false))
                ctx.warn("Conos con canto redondeado: se importan sin el redondeo");
            out = c4d::makeCone(bc->GetFloat(PRIM_CONE_TRAD, 0.0), bc->GetFloat(PRIM_CONE_BRAD, 100.0),
                                bc->GetFloat(PRIM_CONE_HEIGHT, 200.0), bc->GetInt32(PRIM_CONE_SEG, 36),
                                bc->GetBool(PRIM_CONE_CAPS, true), axis);
            return true;
        case Otorus:
            out = c4d::makeTorus(bc->GetFloat(PRIM_TORUS_OUTERRAD, 200.0), bc->GetFloat(PRIM_TORUS_INNERRAD, 50.0),
                                 bc->GetInt32(PRIM_TORUS_SEG, 36), bc->GetInt32(PRIM_TORUS_CSUB, 18), axis);
            return true;
        case Odisc:
            out = c4d::makeDisc(bc->GetFloat(PRIM_DISC_IRAD, 0.0), bc->GetFloat(PRIM_DISC_ORAD, 100.0),
                                bc->GetInt32(PRIM_DISC_SEG, 36), axis);
            return true;
        case Opyramid: {
            const cineware::Vector v = bc->GetVector(PRIM_PYRAMID_LEN, cineware::Vector(200.0, 200.0, 200.0));
            out = c4d::makePyramid(vec(v));
            return true;
        }
        case Osinglepoly:
            if (bc->GetBool(PRIM_POLY_TRIANG, false)) ctx.warn("Primitiva Polígono en modo triángulo: se importa como cuadrilátero");
            out = c4d::makePlane(bc->GetFloat(PRIM_POLY_WIDTH, 200.0), bc->GetFloat(PRIM_POLY_HEIGHT, 200.0), axis);
            return true;
        default:
            return false;
    }
}

// Caras planas por defecto para lo que en C4D son cajas; suave para el resto.
double defaultPrimitivePhong(Int32 type) {
    return (type == Ocube || type == Opyramid) ? -1.0 : kDefaultPhongRad;
}

// Parametros de objeto (2026-11-11): luces y camaras guardan los suyos en
// su NodeData (LightObjectData, CameraObjectData), no en el contenedor, asi
// que GetDataInstance() no los trae y salian todos con el valor por defecto
// (toda luz era una Omni blanca al 100 %). GetParameter pasa por el NodeData;
// si no responde, se mira el contenedor.
// Datos de una Luz de C4D (distancias en cm -> unidades de Iron3D).
LightData lightFrom(BaseObject* op, Ctx& ctx) {
    BaseContainer* bc = op->GetDataInstance();
    // Diagnostico (IRON3D_DUMP_LIGHTS=1): todos los parametros de la luz tal cual.
    if (std::getenv("IRON3D_DUMP_LIGHTS")) {
        std::fprintf(stderr, "[luz] %s:", op->GetName().GetCStringCopy());
        for (Int32 i = 0;; ++i) {
            const Int32 id = bc->GetIndexId(i);
            if (id == NOTOK) break;
            const GeData* d = bc->GetIndexData(i);
            if (!d) continue;
            switch (d->GetType()) {
                case DA_LONG: std::fprintf(stderr, " %d=%d", (int)id, (int)d->GetInt32()); break;
                case DA_REAL: std::fprintf(stderr, " %d=%.3f", (int)id, d->GetFloat()); break;
                case DA_VECTOR: {
                    const cineware::Vector v = d->GetVector();
                    std::fprintf(stderr, " %d=(%.2f %.2f %.2f)", (int)id, v.x, v.y, v.z);
                    break;
                }
                default: std::fprintf(stderr, " %d:t%d", (int)id, (int)d->GetType()); break;
            }
        }
        std::fprintf(stderr, "\n");
    }
    LightData l;
    const Int32 type = paramInt(op, LIGHT_TYPE, LIGHT_TYPE_OMNI);
    l.kind = (LightKind)std::clamp((int)type, 0, 9);
    const cineware::Vector c = paramVector(op, LIGHT_COLOR, cineware::Vector(1.0, 1.0, 1.0));
    l.color = glm::vec3((float)c.x, (float)c.y, (float)c.z);
    l.intensity = (float)paramFloat(op, LIGHT_BRIGHTNESS, 1.0);
    const double toDeg = 180.0 / kPi;
    l.outerAngleDeg = (float)(paramFloat(op, LIGHT_DETAILS_OUTERANGLE, 30.0 / toDeg) * toDeg);
    l.innerAngleDeg = (float)(paramFloat(op, LIGHT_DETAILS_INNERANGLE, 0.0) * toDeg);
    l.shadow = paramInt(op, LIGHT_SHADOWTYPE, LIGHT_SHADOWTYPE_NONE);
    l.falloff = paramInt(op, LIGHT_DETAILS_FALLOFF, LIGHT_DETAILS_FALLOFF_NONE);
    l.falloffRadius = (float)(paramFloat(op, LIGHT_DETAILS_OUTERDISTANCE, 500.0) / c4d::kCmPerUnit);
    l.innerRadius = (float)(paramFloat(op, LIGHT_DETAILS_INNERDISTANCE, 0.0) / c4d::kCmPerUnit);
    l.noIllumination = paramBool(op, LIGHT_NOLIGHTRADIATION, false);
    l.diffuse = paramBool(op, LIGHT_DETAILS_DIFFUSE, true);
    l.specular = paramBool(op, LIGHT_DETAILS_SPECULAR, true);
    l.areaShape = paramInt(op, LIGHT_AREADETAILS_SHAPE, LIGHT_AREADETAILS_SHAPE_RECTANGLE);
    l.areaSize = glm::vec3((float)(paramFloat(op, LIGHT_AREADETAILS_SIZEX, 200.0) / c4d::kCmPerUnit),
                           (float)(paramFloat(op, LIGHT_AREADETAILS_SIZEY, 200.0) / c4d::kCmPerUnit),
                           (float)(paramFloat(op, LIGHT_AREADETAILS_SIZEZ, 200.0) / c4d::kCmPerUnit));
    if (op->GetTag(Tsunexpression)) ctx.warn("Luces con expresión Sol: se importan como su luz (sin la expresión)");
    return l;
}

CameraData cameraFrom(BaseObject* op) {
    CameraData c;
    c.projection = paramInt(op, CAMERA_PROJECTION, Pperspective);
    c.focalMm = (float)paramFloat(op, CAMERA_FOCUS, 36.0);
    c.sensorMm = (float)paramFloat(op, CAMERAOBJECT_APERTURE, 36.0);
    c.zoom = (float)paramFloat(op, CAMERA_ZOOM, 1.0);
    return c;
}

// Ruta por nombres desde la raiz del documento ("Grupo/Cubo").
std::string namePath(BaseObject* op) {
    std::string path;
    for (BaseObject* o = op; o; o = o->GetUp()) path = utf8(o->GetName()) + (path.empty() ? "" : "/" + path);
    return path;
}

// Pistas de Posicion/Rotacion/Escala -> pistas de Iron3D. C4D guarda una
// curva por componente; Iron3D una clave con los tres valores por fotograma:
// en los fotogramas donde solo hay clave en alguna componente, las otras se
// evaluan en su curva (o se toma el valor fijo si no tienen).
void importTracks(BaseObject* op, SceneNode& node, Ctx& ctx) {
    CCurve* curves[3][3] = {};
    bool any = false;
    for (CTrack* tr = op->GetFirstCTrack(); tr; tr = tr->GetNext()) {
        const DescID& id = tr->GetDescriptionID();
        if (id.GetDepth() < 2) {
            ctx.warn("Pistas de animación de otros parámetros (no Posición/Rotación/Escala): se ignoran");
            continue;
        }
        const Int32 a = id[0].id, b = id[1].id - VECTOR_X;
        const int ch = a == ID_BASEOBJECT_REL_POSITION ? 0 : a == ID_BASEOBJECT_REL_ROTATION ? 1 : a == ID_BASEOBJECT_REL_SCALE ? 2 : -1;
        if (ch < 0 || b < 0 || b > 2) {
            ctx.warn("Pistas de animación de otros parámetros (no Posición/Rotación/Escala): se ignoran");
            continue;
        }
        CCurve* cv = tr->GetCurve(CCURVE_CURVE, false);
        if (!cv || cv->GetKeyCount() == 0) continue;
        curves[ch][b] = cv;
        any = true;
    }
    if (!any) return;
    ++ctx.report->animated;
    const cineware::Vector rel[3] = {op->GetRelPos(), op->GetRelRot(), op->GetRelScale()};
    AnimTrack* tracks[3] = {&node.animPos, &node.animRot, &node.animScale};
    for (int ch = 0; ch < 3; ++ch) {
        std::vector<int> frames;
        for (int b = 0; b < 3; ++b)
            if (CCurve* cv = curves[ch][b])
                for (Int32 k = 0; k < cv->GetKeyCount(); ++k) frames.push_back(cv->GetKey(k)->GetTime().GetFrame(ctx.fps));
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        for (int f : frames) {
            double v[3] = {rel[ch].x, rel[ch].y, rel[ch].z};
            for (int b = 0; b < 3; ++b)
                if (CCurve* cv = curves[ch][b]) v[b] = cv->GetValue(BaseTime((Float)f, ctx.fps));
            glm::vec3 iron;
            if (ch == 0) iron = glm::vec3((float)(v[0] / c4d::kCmPerUnit), (float)(v[1] / c4d::kCmPerUnit), (float)(-v[2] / c4d::kCmPerUnit));
            else if (ch == 1) iron = glm::vec3((float)(v[1] * 180.0 / kPi), (float)(v[0] * 180.0 / kPi), (float)(-v[2] * 180.0 / kPi));
            else iron = glm::vec3((float)v[0], (float)v[1], (float)v[2]);
            tracks[ch]->set(f, iron);
            // Interpolacion de la clave (la del primer componente que tenga clave en f).
            KeyInterp ki = KeyInterp::Spline;
            for (int b = 0; b < 3; ++b) {
                CCurve* cv = curves[ch][b];
                if (!cv) continue;
                bool found = false;
                for (Int32 k = 0; k < cv->GetKeyCount() && !found; ++k) {
                    const CKey* key = cv->GetKey(k);
                    if (key->GetTime().GetFrame(ctx.fps) != f) continue;
                    found = true;
                    const CINTERPOLATION ci = key->GetInterpolation();
                    ki = ci == CINTERPOLATION_LINEAR ? KeyInterp::Linear : ci == CINTERPOLATION_STEP ? KeyInterp::Step : KeyInterp::Spline;
                }
                if (found) break;
            }
            for (AnimKey& k : tracks[ch]->keys)
                if (k.frame == f) k.interp = ki;
        }
    }
}

struct PendingTarget {
    SceneNode* camera;
    BaseObject* target;
};

// Generadores Simetria y Subdivision Surface sin cache en el archivo
// (2026-11-11): las .lib4d no guardan la malla generada, y antes salian como
// Nulo con el hijo en crudo (medio torso, piezas en bloque). Se evaluan aqui
// sobre su primer hijo, como hace C4D, tambien encadenados (SDS > Simetria >
// Cubo). La geometria sale en el espacio local de `op`, con el material, la
// Phong y las partes por Seleccion de Poligonos del objeto de entrada.
struct GeneratedInput {
    c4d::PolySoup soup;
    std::vector<int> slot;            // parte de cada poligono (-1 = material del objeto)
    std::vector<MaterialPart> parts;
    int material = -1;
    TextureTagData texTag;
    double phong = -1.0;
    bool subdivided = false;
};

bool isEvaluatedGenerator(BaseObject* op) {
    const Int32 t = op->GetType();
    return (t == Osds || t == Osymmetry) && !op->GetCache() && !op->GetDeformCache() && op->GetDeformMode();
}

bool generatedGeometry(BaseObject* op, int inheritedMaterial, const TextureTagData& inheritedTag, Ctx& ctx,
                       GeneratedInput& out, int depth);

// Hijos de un Nulo unidos en una malla (en el espacio del nulo). El material
// de cada pieza pasa a ser una parte con el nombre de la pieza; sus propias
// partes por seleccion se conservan.
bool mergedHierarchy(BaseObject* nullOp, int inheritedMaterial, const TextureTagData& inheritedTag, Ctx& ctx,
                     GeneratedInput& out, int depth) {
    if (depth > 16) return false;
    TextureTagData tag = inheritedTag;
    out.material = materialOfObject(nullOp, inheritedMaterial, tag, ctx);
    out.texTag = tag;
    for (BaseObject* ch = nullOp->GetDown(); ch; ch = ch->GetNext()) {
        GeneratedInput piece;
        const bool ok = ch->GetType() == Onull ? mergedHierarchy(ch, out.material, tag, ctx, piece, depth + 1)
                                               : generatedGeometry(ch, out.material, tag, ctx, piece, depth + 1);
        if (!ok || piece.soup.empty()) continue;
        const int own = (int)out.parts.size();
        MaterialPart whole;
        whole.materialIndex = piece.material;
        whole.texTag = piece.texTag;
        whole.selection = utf8(ch->GetName());
        out.parts.push_back(whole);
        const int offset = (int)out.parts.size();
        for (MaterialPart& p : piece.parts) out.parts.push_back(std::move(p));
        out.soup.append(piece.soup, mat(ch->GetMl()));
        for (int s : piece.slot) out.slot.push_back(s < 0 ? own : offset + s);
        if (out.phong < 0.0) out.phong = piece.phong;
    }
    return !out.soup.empty() && out.slot.size() == out.soup.polys.size();
}

bool generatedGeometry(BaseObject* op, int inheritedMaterial, const TextureTagData& inheritedTag, Ctx& ctx,
                       GeneratedInput& out, int depth) {
    if (!op || depth > 16) return false;
    TextureTagData tag = inheritedTag;
    const int material = materialOfObject(op, inheritedMaterial, tag, ctx);
    const Int32 type = op->GetType();

    if (type == Opolygon) {
        auto* po = static_cast<PolygonObject*>(op);
        std::vector<cineware::Vector> skinned;
        if (skinnedPoints(po, skinned)) appendPolygons(po, glm::dmat4(1.0), out.soup, &skinned);
        else appendPolygons(po, glm::dmat4(1.0), out.soup);
        selectionParts(op, po->GetPolygonCount(), out.parts, out.slot, ctx);
        if (out.slot.size() != out.soup.polys.size()) {
            out.parts.clear();
            out.slot.assign(out.soup.polys.size(), -1);
        }
        out.material = material;
        out.texTag = tag;
        out.phong = phongAngle(op);
        return !out.soup.empty();
    }
    if (!isEvaluatedGenerator(op)) {
        // Primitiva o generador con cache: su geometria tal cual.
        if (!geometryFromCache(op, out.soup) && !rebuildPrimitive(op, out.soup, ctx)) return false;
        out.slot.assign(out.soup.polys.size(), -1);
        out.material = material;
        out.texTag = tag;
        out.phong = phongAngle(op);
        if (out.phong < 0.0) out.phong = defaultPrimitivePhong(type);
        return true;
    }

    // Simetria / SDS: el primer hijo es la entrada (los demas hijos quedan aparte).
    BaseObject* in = op->GetDown();
    if (!in) return false;
    GeneratedInput child;
    if (in->GetType() == Onull && type == Osymmetry) {
        // Simetria sobre un Nulo: refleja toda su jerarquia. Cada pieza conserva
        // su material como parte de la malla unida.
        if (!mergedHierarchy(in, material, tag, ctx, child, depth + 1)) return false;
    } else if (!generatedGeometry(in, material, tag, ctx, child, depth + 1)) {
        return false;
    }
    out = std::move(child);
    c4d::PolySoup placed;
    placed.append(out.soup, mat(in->GetMl()));
    out.soup = std::move(placed);

    if (type == Osymmetry) {
        // SYMMETRYOBJECT_PLANE: 0 XY (refleja Z), 1 ZY (refleja X), 2 XZ (refleja Y).
        const Int32 plane = paramInt(op, SYMMETRYOBJECT_PLANE, SYMMETRYOBJECT_PLANE_YZ);
        const int axis = plane == SYMMETRYOBJECT_PLANE_XY ? 2 : plane == SYMMETRYOBJECT_PLANE_XZ ? 1 : 0;
        const bool weld = paramBool(op, SYMMETRYOBJECT_WELD, true);
        const double tol = paramFloat(op, SYMMETRYOBJECT_TOLERANCE, 0.01);
        out.soup = c4d::mirrorSoup(out.soup, axis, weld, tol, &out.slot);
    } else {
        // Nivel del editor: el visor de C4D ensena este, no el de render.
        const int levels = std::clamp<int>(paramInt(op, SDSOBJECT_SUBEDITOR_CM, 2), 0, 4);
        const Int32 kind = paramInt(op, SDSOBJECT_TYPE, SDSOBJECT_TYPE_CM);
        if (kind == SDSOBJECT_TYPE_OSD_LOOP || kind == SDSOBJECT_TYPE_OSD_BILINEAR)
            ctx.warn("Subdivision Surface tipo Loop o Bilineal: se subdivide con Catmull-Clark");
        out.soup = c4d::subdivideCatmullClark(out.soup, levels, &out.slot);
        out.subdivided = true;
        // La malla subdividida se ve suave en C4D aunque la jaula no tenga Phong.
        if (out.phong < 0.0) out.phong = kDefaultPhongRad;
    }
    return !out.soup.empty();
}

void importObject(BaseObject* op, SceneNode& parent, int inheritedMaterial, const TextureTagData& inheritedTag,
                  bool isGeneratorInput, Ctx& ctx, std::vector<PendingTarget>& targets) {
    C4DImportReport& rep = *ctx.report;
    const std::string name = utf8(op->GetName());
    SceneNode* node = parent.addChild(name.empty() ? std::string("Objeto") : name);
    ++rep.objects;

    // Transformacion: los numeros de C4D tal cual (HPB incluido) si no hay
    // transformacion congelada; si la hay, desde la matriz.
    const cineware::Vector fp = op->GetFrozenPos(), fr = op->GetFrozenRot(), fs = op->GetFrozenScale();
    const bool frozen = fp != cineware::Vector(0.0) || fr != cineware::Vector(0.0) || fs != cineware::Vector(1.0);
    if (!frozen) {
        const cineware::Vector rp = op->GetRelPos(), rr = op->GetRelRot(), rs = op->GetRelScale();
        node->position = glm::vec3((float)(rp.x / c4d::kCmPerUnit), (float)(rp.y / c4d::kCmPerUnit), (float)(-rp.z / c4d::kCmPerUnit));
        // B cambia de signo al pasar a Iron3D (2026-11-11): medido contra la
        // matriz Ml de C4D en los 97 objetos girados de Toy Foxy, solo
        // (P, H, -B) la reproduce. Con +B, la oreja izquierda flotaba lejos.
        node->rotationDeg = glm::vec3((float)(rr.y * 180.0 / kPi), (float)(rr.x * 180.0 / kPi), (float)(-rr.z * 180.0 / kPi));
        node->scale = glm::vec3((float)rs.x, (float)rs.y, (float)rs.z);
    } else {
        const cineware::Matrix& ml = op->GetMl();
        const c4d::TRS trs = c4d::toIronTRS(vec(ml.off), vec(ml.v1), vec(ml.v2), vec(ml.v3));
        node->position = trs.position;
        node->rotationDeg = trs.rotationDeg;
        node->scale = trs.scale;
        ctx.warn("Objetos con transformación congelada: se importa la transformación final (sin la congelada aparte)");
    }
    node->editorVis = visFrom(op->GetEditorMode());
    node->renderVis = visFrom(op->GetRenderMode());
    if (isGeneratorInput) {
        // Objeto de entrada de un generador importado con su geometria final:
        // en C4D no se ve por separado, asi que aqui tampoco.
        node->editorVis = node->renderVis = VisMode::Off;
    }
    TextureTagData texTag = inheritedTag;
    const int material = materialOfObject(op, inheritedMaterial, texTag, ctx);
    node->materialIndex = material;
    node->texTag = texTag;
    importTracks(op, *node, ctx);

    const Int32 type = op->GetType();
    c4d::PolySoup soup;
    std::vector<MaterialPart> parts;
    std::vector<int> polySlot;
    double phong = phongAngle(op);
    bool childrenAreInputs = false;
    bool firstChildIsInput = false; // Simetria / SDS evaluados: solo el primer hijo es su entrada

    if (type == Opolygon) {
        BaseObject* dc = op->GetDeformCache();
        PolygonObject* po = (dc && dc->GetType() == Opolygon) ? static_cast<PolygonObject*>(dc)
                                                               : static_cast<PolygonObject*>(op);
        std::vector<cineware::Vector> skinned;
        if (po == op && skinnedPoints(po, skinned)) {
            appendPolygons(po, glm::dmat4(1.0), soup, &skinned);
            ctx.warn("Piel: aplicada con la pose de las articulaciones guardada en el archivo (todavía sin animación "
                     "ni morphs de Pose Morph)");
        } else {
            appendPolygons(po, glm::dmat4(1.0), soup);
        }
        // La cache de deformacion no siempre lleva la etiqueta UVW: la del objeto vale.
        if (soup.uvs.empty() && po != op) {
            c4d::PolySoup own;
            appendPolygons(static_cast<PolygonObject*>(op), glm::dmat4(1.0), own);
            if (own.polys.size() == soup.polys.size()) soup.uvs = own.uvs;
        }
        selectionParts(op, static_cast<PolygonObject*>(op)->GetPolygonCount(), parts, polySlot, ctx);
        if (polySlot.size() != soup.polys.size()) parts.clear();
    } else if (type == Olight) {
        node->primitive = PrimitiveType::Light;
        node->light = lightFrom(op, ctx);
        ++rep.lights;
    } else if (type == Ocamera) {
        node->primitive = PrimitiveType::Camera;
        node->camera = cameraFrom(op);
        if (BaseTag* t = op->GetTag(Ttargetexpression))
            if (BaseList2D* link = t->GetDataInstance()->GetLink(TARGETEXPRESSIONTAG_LINK))
                targets.push_back({node, static_cast<BaseObject*>(link)});
        if (node->camera.projection > Pparallel)
            ctx.warn("Cámaras con proyección especial (Izquierda, Isométrica...): se conserva, el visor la trata como paralela");
        ++rep.cameras;
    } else if (type == Onull) {
        // grupo, nada mas
    } else if (isEvaluatedGenerator(op)) {
        GeneratedInput gen;
        if (generatedGeometry(op, inheritedMaterial, inheritedTag, ctx, gen, 0)) {
            soup = std::move(gen.soup);
            parts = std::move(gen.parts);
            polySlot = std::move(gen.slot);
            if (polySlot.size() != soup.polys.size()) parts.clear();
            phong = gen.phong;
            node->materialIndex = gen.material;
            node->texTag = gen.texTag;
            firstChildIsInput = true;
            ++rep.generatorsEvaluated;
        } else {
            ctx.warn(typeLabel(op) + ": su primer hijo no tiene geometría que generar; se importa como Nulo");
        }
    } else if (geometryFromCache(op, soup)) {
        ++rep.generatorsFromCache;
        childrenAreInputs = op->GetDown() != nullptr;
    } else if (rebuildPrimitive(op, soup, ctx)) {
        ++rep.primitivesRebuilt;
        if (phong < 0.0) phong = defaultPrimitivePhong(type);
    } else if (type == Ospline || type == Osplineprimitive || (type >= Osplineprofile && type <= Osplinecontour)) {
        ctx.warn("Splines: Iron3D todavía no tiene splines; se importan como Nulo (conservan posición)");
    } else if (type == Osky || type == Ofloor || type == Oenvironment || type == Obackground ||
               type == Oforeground || type == Ostage) {
        ctx.warn("Objetos de escena (" + typeLabel(op) + "...): Iron3D todavía no los tiene; se importan como Nulo");
    } else if (type == Ojoint) {
        ctx.warn("Articulaciones (rigging): Iron3D todavía no tiene esqueletos; se importan como Nulo (conservan posición)");
    } else if (type == Oskin || op->IsInstanceOf(Obasedeform)) {
        ctx.warn("Deformadores (también Piel): su efecto solo llega si C4D guardó la malla deformada; el deformador se "
                 "importa como Nulo");
    } else {
        ctx.warn(typeLabel(op) + ": sin geometría guardada en el archivo, se importa como Nulo "
                 "(guarda el .c4d con \"Guardar polígonos para Melange\" activado para traerla)");
    }

    // Poligonos agrupados por parte (estable: primero los del material del
    // objeto, luego cada parte en orden), para que cada una sea un rango.
    std::vector<int> slotOfPoly;
    if (!parts.empty()) {
        std::vector<size_t> order(soup.polys.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return polySlot[x] < polySlot[y]; });
        c4d::PolySoup sorted;
        sorted.points = std::move(soup.points);
        sorted.polys.reserve(order.size());
        for (size_t i : order) sorted.polys.push_back(soup.polys[i]);
        if (!soup.uvs.empty())
            for (size_t i : order) sorted.uvs.push_back(soup.uvs[i]);
        for (size_t i : order) slotOfPoly.push_back(polySlot[i]);
        soup = std::move(sorted);
    }
    if (!soup.empty()) {
        std::vector<uint32_t> trisPerPoly;
        auto mesh = std::make_shared<MeshData>(c4d::toIronMesh(soup, phong, parts.empty() ? nullptr : &trisPerPoly));
        if (!parts.empty() && trisPerPoly.size() == slotOfPoly.size()) {
            uint32_t idx = 0;
            for (size_t i = 0; i < slotOfPoly.size(); ++i) {
                const uint32_t n = trisPerPoly[i] * 3u;
                if (slotOfPoly[i] >= 0) {
                    MaterialPart& part = parts[(size_t)slotOfPoly[i]];
                    if (part.indexCount == 0) part.firstIndex = idx;
                    part.indexCount += n;
                }
                idx += n;
            }
            for (const MaterialPart& part : parts)
                if (part.indexCount > 0) node->materialParts.push_back(part);
            std::sort(node->materialParts.begin(), node->materialParts.end(),
                      [](const MaterialPart& x, const MaterialPart& y) { return x.firstIndex < y.firstIndex; });
        }
        rep.points += (long long)soup.points.size();
        rep.polygons += (long long)soup.polys.size();
        if (!mesh->indices.empty()) {
            node->primitive = PrimitiveType::Mesh;
            node->mesh = std::move(mesh);
            ++rep.meshes;
        }
    }

    for (BaseObject* ch = op->GetDown(); ch; ch = ch->GetNext())
        importObject(ch, *node, material, texTag,
                     isGeneratorInput || childrenAreInputs || (firstChildIsInput && ch == op->GetDown()), ctx, targets);
}

} // namespace

bool cinewareAvailable() { return true; }

bool isC4DPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".c4d";
}

bool importC4D(const std::filesystem::path& file, SceneNode& parent, std::vector<iron3d::Material>& materials,
               C4DImportReport& report, std::string* outError) {
    report = C4DImportReport{};
    report.fileName = pathUtf8(file.filename());

    // Ruta en UTF-16: con el code page local fallan las rutas con acentos o ñ.
    const std::u16string wide = file.u16string();
    Filename fn{cineware::String(reinterpret_cast<const UInt16*>(wide.c_str()))};
    BaseDocument* doc = LoadDocument(fn, SCENEFILTER_OBJECTS | SCENEFILTER_MATERIALS);
    if (!doc) {
        if (outError) *outError = "Cineware no ha podido leer el archivo (¿no es un .c4d o está dañado?)";
        return false;
    }

    Ctx ctx;
    ctx.report = &report;
    ctx.materialBase = (int)materials.size();
    ctx.docDir = pathUtf8(file.parent_path());
    ctx.fps = doc->GetFps() > 0 ? (Float)doc->GetFps() : 30.0;
    SceneNode temp("__c4d_import_tmp__");
    std::vector<PendingTarget> targets;
    for (BaseObject* op = doc->GetFirstObject(); op; op = op->GetNext())
        importObject(op, temp, -1, TextureTagData{}, false, ctx, targets);
    // Etiqueta Objetivo: el objetivo se guarda por su ruta de nombres.
    for (const PendingTarget& t : targets) t.camera->camera.target = namePath(t.target);
    BaseDocument::Free(doc);

    for (const auto& w : ctx.warnings)
        report.warnings.push_back(w.second > 1 ? w.first + " (" + std::to_string(w.second) + ")" : w.first);
    for (iron3d::Material& m : ctx.materials) materials.push_back(std::move(m));
    parent.appendChildrenFrom(temp);
    return true;
}

} // namespace iron3d
