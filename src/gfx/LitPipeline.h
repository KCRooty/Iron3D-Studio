#pragma once

// Pipeline con iluminacion PBR (Fase 5, pasado a metallic/roughness
// 2026-10-30, ver ROADMAP_RENDER.md fase 1): Cook-Torrance (GGX + Smith +
// Fresnel-Schlick), una sola luz puntual, sin texturas ni sombras todavia.
// Separado de BasicPipeline (que sigue intacto, sirve solo al gizmo) para no
// arriesgar el render ya verificado del gizmo con este cambio.
//
// Matrices + luz + material siguen yendo en push constants (240 bytes tras
// la Fase render 2 -- por encima del minimo garantizado de Vulkan (128B),
// todavia por debajo de lo que soporta cualquier GPU de escritorio real
// (tipicamente 256B); se comprueba VkPhysicalDeviceLimits::maxPushConstantsSize
// una vez en create() y se aborta con un mensaje claro si la GPU no llega).
// El UNICO descriptor set que tiene esta pipeline es el del mapa de sombras
// (ShadowMap::descriptorSetLayout(), un combined-image-sampler) -- se le
// pasa ya creado desde fuera (VulkanRenderer crea el ShadowMap antes que
// esta pipeline) en vez de que LitPipeline sepa nada de como se genero.

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <filesystem>

namespace iron3d {

struct Material;

// 2026-11-10 (texturas en el visor): 256 bytes justos. La intensidad
// ambiente, que siempre fue 0.15, pasa a ser constante de lit.frag y su hueco
// (lightPos.w) lleva los indicadores de textura; uvTransform es el vec4 nuevo.
struct LitPushConstants {
    glm::mat4 model;
    glm::mat4 mvp;
    glm::vec4 lightPosAndFlags;      // xyz = posicion de luz en mundo, w = kLitTex* (entero como float)
    glm::vec4 eyePosAndRoughness;    // xyz = posicion de camara en mundo, w = roughness [0,1]
    glm::vec4 baseColorAndMetallic;  // xyz = color base (albedo) del material, w = metallic [0,1]
    glm::mat4 lightViewProj;         // Fase render 2: vista*proyeccion de la luz (lit.vert la usa para fragPosLightSpace)
    glm::vec4 uvTransform;           // uv de la imagen = uv * xy + zw
};
static_assert(sizeof(LitPushConstants) == 256, "lit.vert/lit.frag esperan 256 bytes");

// Indicadores de LitPushConstants::lightPosAndFlags.w (mismos valores que lit.frag).
constexpr int kLitTexColor = 1;
constexpr int kLitTexTile = 2;

class LitPipeline {
public:
    void create(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                const std::filesystem::path& shaderDir, VkDescriptorSetLayout shadowSetLayout,
                VkDescriptorSetLayout aoSetLayout, VkDescriptorSetLayout colorTexSetLayout,
                VkCullModeFlags cullMode = VK_CULL_MODE_NONE);
    void destroy(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    // set=0: el combined-image-sampler del mapa de sombras (ver ShadowMap).
    void bindShadowMap(VkCommandBuffer cmd, VkDescriptorSet shadowSet) const;
    // set=1: el combined-image-sampler de la oclusion ambiental, ya
    // difuminada (Fase 3, ver SsaoPass).
    void bindAoMap(VkCommandBuffer cmd, VkDescriptorSet aoSet) const;
    // set=2: la textura de Color del material (2026-11-10, ver MaterialTextures).
    void bindColorTexture(VkCommandBuffer cmd, VkDescriptorSet texSet) const;
    // `texFlags` = kLitTex*; `uvTransform` = escala (xy) y desplazamiento (zw).
    void pushConstants(VkCommandBuffer cmd, const glm::mat4& model, const glm::mat4& mvp,
                        const glm::vec3& lightPosWS, const glm::vec3& eyePosWS, const Material& material,
                        const glm::mat4& lightViewProj, int texFlags, const glm::vec4& uvTransform) const;

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace iron3d
