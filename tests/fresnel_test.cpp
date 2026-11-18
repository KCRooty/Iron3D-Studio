// Shader Fresnel de C4D (2026-11-18): sin GPU. Comprueba que el canal Color con un Fresnel raiz se hornea
// a una tira 256x1 (texel i = canal para |N.V| = i/255), que el modo fisico da Schlick y que el arbol
// sobrevive al ida y vuelta de texto del .iron.
#include "core/ChannelBake.h"
#include "core/Material.h"
#include "core/ShaderTree.h"

#include <cmath>
#include <cstdio>

using namespace iron3d;

static int failures = 0;
#define CHECK(c, msg)                                          \
    do {                                                       \
        if (!(c)) {                                            \
            std::fprintf(stderr, "FALLO: %s\n", msg);          \
            ++failures;                                        \
        }                                                      \
    } while (0)

static ShaderNode gradientFresnel() {
    ShaderNode n;
    n.kind = ShaderKind::Fresnel;
    n.c4dType = 1011103;
    n.input = 2005;
    GradientKnot a; // en x = 0 (de canto): rojo
    a.color = {1.0f, 0.0f, 0.0f};
    a.pos = 0.0f;
    a.interpolation = 3;
    GradientKnot b; // en x = 1 (de cara): azul
    b.color = {0.0f, 0.0f, 1.0f};
    b.pos = 1.0f;
    b.interpolation = 3;
    n.gradient = {a, b};
    return n;
}

int main() {
    Material m;
    ChannelSettings& c = m.channel(TexSlot::Color);
    c.shader = gradientFresnel();

    CHECK(channelHasTexture(m, TexSlot::Color), "el Fresnel raiz cuenta como textura del canal Color");
    CHECK(channelIsFresnel(m, TexSlot::Color), "channelIsFresnel");
    CHECK(!channelIsFresnel(m, TexSlot::Roughness), "el Fresnel solo vale en el canal Color");

    TextureImage img;
    CHECK(loadChannelTexture(m, TexSlot::Color, img), "se hornea la tira");
    CHECK(img.width == 256 && img.height == 1, "la tira es 256x1");
    if (img.width == 256 && img.height == 1) {
        CHECK(img.rgba[0] > 250 && img.rgba[2] < 5, "x = 0 (de canto) es rojo");
        CHECK(img.rgba[255 * 4] < 5 && img.rgba[255 * 4 + 2] > 250, "x = 1 (de cara) es azul");
        const int mid = img.rgba[128 * 4];
        CHECK(mid > 100 && mid < 160, "a medias, mezcla lineal");
    }

    // Modo fisico: Schlick con IOR 1.333 -> F0 ~ 0.02; de canto ~ 1.
    c.shader.fresnelPhysical = true;
    c.shader.fresnelIor = 1.333f;
    TextureImage ph;
    CHECK(loadChannelTexture(m, TexSlot::Color, ph) && ph.width == 256, "tira fisica");
    if (ph.width == 256) {
        CHECK(ph.rgba[255 * 4] < 12, "fisico: de cara ~ F0 (bajo)");
        CHECK(ph.rgba[0] > 245, "fisico: de canto ~ 1");
    }

    // Modo Multiplicar (3) con color del canal: base * fresnel.
    c.shader.fresnelPhysical = false;
    c.color = {0.5f, 0.5f, 0.5f};
    c.mixMode = 3;
    TextureImage mul;
    CHECK(loadChannelTexture(m, TexSlot::Color, mul) && mul.width == 256, "tira multiplicada");
    if (mul.width == 256) CHECK(mul.rgba[0] > 120 && mul.rgba[0] < 135, "multiplicar: 0.5 * 1 = 0.5 de canto en rojo");

    // La clave de cache cambia con el degradado.
    const std::string k1 = channelTextureKey(m, TexSlot::Color);
    c.shader.gradient[0].color = {0.0f, 1.0f, 0.0f};
    CHECK(channelTextureKey(m, TexSlot::Color) != k1, "la clave de cache cambia con el degradado");

    // Ida y vuelta de texto.
    const std::string text = encodeShaderTree(c.shader);
    ShaderNode back;
    CHECK(decodeShaderTree(text, back), "decodeShaderTree");
    CHECK(back.kind == ShaderKind::Fresnel && back.input == 2005 && back.gradient.size() == 2, "ida y vuelta");
    CHECK(encodeShaderTree(back) == text, "el texto es estable");

    if (failures == 0) std::puts("fresnel: todo correcto");
    return failures == 0 ? 0 : 1;
}
