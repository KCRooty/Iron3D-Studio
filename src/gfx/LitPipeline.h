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

struct LitPushConstants {
    glm::mat4 model;
    glm::mat4 mvp;
    glm::vec4 lightPosAndAmbient;    // xyz = posicion de luz en mundo, w = intensidad ambiente (hack temporal, sin GI real)
    glm::vec4 eyePosAndRoughness;    // xyz = posicion de camara en mundo, w = roughness [0,1]
    glm::vec4 baseColorAndMetallic;  // xyz = color base (albedo) del material, w = metallic [0,1]
    glm::mat4 lightViewProj;         // Fase render 2: vista*proyeccion de la luz (lit.vert la usa para fragPosLightSpace)
};

class LitPipeline {
public:
    void create(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                const std::filesystem::path& shaderDir, VkDescriptorSetLayout shadowSetLayout,
                VkDescriptorSetLayout aoSetLayout, VkCullModeFlags cullMode = VK_CULL_MODE_NONE);
    void destroy(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    // set=0: el combined-image-sampler del mapa de sombras (ver ShadowMap).
    void bindShadowMap(VkCommandBuffer cmd, VkDescriptorSet shadowSet) const;
    // set=1: el combined-image-sampler de la oclusion ambiental, ya
    // difuminada (Fase 3, ver SsaoPass).
    void bindAoMap(VkCommandBuffer cmd, VkDescriptorSet aoSet) const;
    void pushConstants(VkCommandBuffer cmd, const glm::mat4& model, const glm::mat4& mvp,
                        const glm::vec3& lightPosWS, const glm::vec3& eyePosWS, const Material& material,
                        const glm::mat4& lightViewProj) const;

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace iron3d
