#pragma once

// Pipeline solo-vertice para la pasada de profundidad del mapa de sombras
// (Fase render 2, ver ROADMAP_RENDER.md): sin fragment shader (no hay
// attachment de color en el render pass de ShadowMap, solo profundidad),
// sin blending, con test+escritura de profundidad. Mismo patron que
// BasicPipeline pero mas simple todavia (un solo push constant: la MVP
// desde el punto de vista de la luz).

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <filesystem>

namespace iron3d {

class ShadowPipeline {
public:
    void create(VkDevice device, VkRenderPass renderPass, const std::filesystem::path& shaderDir);
    void destroy(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    void pushLightMvp(VkCommandBuffer cmd, const glm::mat4& lightMvp) const;

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace iron3d
