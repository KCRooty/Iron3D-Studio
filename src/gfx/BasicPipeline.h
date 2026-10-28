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
    // `topology` por defecto TRIANGLE_LIST (el cubo); el gizmo de transformacion
    // (Fase 2b) crea dos instancias mas sobre el mismo shader/vertex layout
    // (posicion+color) y push constant MVP: LINE_LIST para los ejes y
    // TRIANGLE_LIST con `cullMode=NONE` para las puntas solidas (conos/cubos),
    // cuyo orden de vertices no se verifico a mano contra VK_CULL_MODE_BACK_BIT.
    void create(VkDevice device, VkRenderPass renderPass, const std::filesystem::path& shaderDir,
                VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT);
    void destroy(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    void pushMvp(VkCommandBuffer cmd, const glm::mat4& mvp) const;

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace iron3d
