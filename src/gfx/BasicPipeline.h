#pragma once

// Pipeline grafico minimo para el cubo de la Fase 1: shaders basic.vert/.frag,
// un push constant con la matriz MVP, sin descriptor sets todavia (llega con
// materiales PBR en fases posteriores).

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <filesystem>

namespace iron3d {

class BasicPipeline {
public:
    void create(VkDevice device, VkRenderPass renderPass, const std::filesystem::path& shaderDir);
    void destroy(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    void pushMvp(VkCommandBuffer cmd, const glm::mat4& mvp) const;

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace iron3d
