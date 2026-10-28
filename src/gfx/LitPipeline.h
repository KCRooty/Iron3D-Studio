#pragma once

// Pipeline con iluminacion basica (Fase 5): Phong clasico (ambiente + difuso
// + especular), una sola luz puntual, sin texturas ni sombras. Separado de
// BasicPipeline (que sigue intacto, sirve solo al gizmo) para no arriesgar
// el render ya verificado del gizmo con este cambio.
//
// Sin descriptor sets: todo (matrices + luz + material) va en push
// constants. 176 bytes en total -- por encima del minimo garantizado de
// Vulkan (128B), pero muy por debajo de lo que soporta cualquier GPU de
// escritorio real (tipicamente 256B); se comprueba
// VkPhysicalDeviceLimits::maxPushConstantsSize una vez en create() y se
// aborta con un mensaje claro si la GPU no llega (no deberia pasar en el
// hardware de escritorio al que apunta este proyecto). Evita toda la
// plomeria de pool/layout/sets/buffers-por-imagen-en-vuelo que haria falta
// con un UBO, y es la que hay para esto: nada cambia por frame salvo lo que
// ya cabe aqui.

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <filesystem>

namespace iron3d {

struct Material;

struct LitPushConstants {
    glm::mat4 model;
    glm::mat4 mvp;
    glm::vec4 lightPosAndAmbient;   // xyz = posicion de luz en mundo, w = intensidad ambiente
    glm::vec4 eyePosAndShininess;   // xyz = posicion de camara en mundo, w = exponente Phong
    glm::vec4 materialDiffuseSpec;  // xyz = color difuso del material, w = intensidad especular
};

class LitPipeline {
public:
    void create(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                const std::filesystem::path& shaderDir, VkCullModeFlags cullMode = VK_CULL_MODE_NONE);
    void destroy(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    void pushConstants(VkCommandBuffer cmd, const glm::mat4& model, const glm::mat4& mvp,
                        const glm::vec3& lightPosWS, const glm::vec3& eyePosWS, const Material& material) const;

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace iron3d
