#include "SwapchainWindow.h"
#include "VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdlib>

namespace iron3d {

namespace {
void checkVk(VkResult err) {
    if (err != 0) {
        std::fprintf(stderr, "[Iron3D][vulkan] VkResult = %d\n", (int)err);
        if (err < 0) std::abort();
    }
}
} // namespace

void SwapchainWindow::init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                            uint32_t queueFamily, VkSurfaceKHR surface, int width, int height,
                            uint32_t minImageCount) {
    wd_ = new ImGui_ImplVulkanH_Window();
    minImageCount_ = minImageCount;
    wd_->Surface = surface;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamily, surface, &supported);
    if (supported != VK_TRUE) {
        std::fprintf(stderr, "[Iron3D] La GPU elegida no soporta presentar en esta superficie.\n");
        std::abort();
    }

    const VkFormat requestFormats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
    wd_->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice, surface, requestFormats, (int)(sizeof(requestFormats) / sizeof(requestFormats[0])),
        VK_COLORSPACE_SRGB_NONLINEAR_KHR);

    VkPresentModeKHR requestModes[] = {VK_PRESENT_MODE_FIFO_KHR};
    wd_->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, surface, requestModes, 1);

    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, wd_, queueFamily,
                                            nullptr, width, height, minImageCount_);
    resetSemaphorePool();
}

void SwapchainWindow::destroy(VkInstance instance, VkDevice device) {
    if (!wd_) return;
    ImGui_ImplVulkanH_DestroyWindow(instance, device, wd_, nullptr);
    delete wd_;
    wd_ = nullptr;
}

void SwapchainWindow::resizeIfNeeded(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                      uint32_t queueFamily, int fbWidth, int fbHeight) {
    if (fbWidth <= 0 || fbHeight <= 0) return;
    if (!swapChainRebuild_ && wd_->Width == fbWidth && wd_->Height == fbHeight) return;

    ImGui_ImplVulkan_SetMinImageCount(minImageCount_);
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, wd_, queueFamily,
                                            nullptr, fbWidth, fbHeight, minImageCount_);
    wd_->FrameIndex = 0;
    swapChainRebuild_ = false;
    resetSemaphorePool();
}

void SwapchainWindow::resetSemaphorePool() {
    imageSemaphoreSlot_.assign(wd_->ImageCount, -1);
    freeSemaphores_.clear();
    for (uint32_t i = 0; i < wd_->SemaphoreCount; ++i) freeSemaphores_.push_back(i);
    currentSemaphoreSlot_ = 0;
}

VkCommandBuffer SwapchainWindow::beginFrame(VkDevice device) {
    if (isMinimized()) return VK_NULL_HANDLE;

    // No usamos wd_->SemaphoreIndex (rotacion ciega, igual que el ejemplo de
    // ImGui): ese contador avanza sin relacion con que imagen real se acabo
    // adquiriendo, asi que el fence-wait de abajo (indexado por FrameIndex,
    // la imagen real) no demuestra nada sobre si ESE semaforo es seguro de
    // reusar -- es justo el hueco que describe la guia de Vulkan del aviso
    // de validacion (swapchain_semaphore_reuse). En vez de eso, pedimos un
    // semaforo libre del pool (hay SemaphoreCount = ImageCount+1, ImGui ya
    // reserva ese colchon) y solo lo devolvemos al pool cuando el fence de
    // la imagen que lo uso la ULTIMA vez ya se esperase aqui mismo.
    uint32_t acquireSlot = freeSemaphores_.back();
    freeSemaphores_.pop_back();
    VkSemaphore imageAcquired = wd_->FrameSemaphores[acquireSlot].ImageAcquiredSemaphore;
    VkResult err = vkAcquireNextImageKHR(device, wd_->Swapchain, UINT64_MAX, imageAcquired,
                                          VK_NULL_HANDLE, &wd_->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        freeSemaphores_.push_back(acquireSlot);
        swapChainRebuild_ = true;
        return VK_NULL_HANDLE;
    }
    checkVk(err);

    ImGui_ImplVulkanH_Frame* fd = &wd_->Frames[wd_->FrameIndex];
    checkVk(vkWaitForFences(device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    checkVk(vkResetFences(device, 1, &fd->Fence));
    checkVk(vkResetCommandPool(device, fd->CommandPool, 0));

    // Aqui ya es seguro: el fence que acabamos de esperar es el de la ultima
    // vez que se dibujo esta misma imagen, asi que el semaforo que tenia
    // asignado entonces ya termino de usarse y puede volver al pool.
    int previousSlot = imageSemaphoreSlot_[wd_->FrameIndex];
    if (previousSlot >= 0) freeSemaphores_.push_back((uint32_t)previousSlot);
    imageSemaphoreSlot_[wd_->FrameIndex] = (int)acquireSlot;
    currentSemaphoreSlot_ = acquireSlot;

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(fd->CommandBuffer, &beginInfo));

    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = wd_->RenderPass;
    rpInfo.framebuffer = fd->Framebuffer;
    rpInfo.renderArea.extent.width = (uint32_t)wd_->Width;
    rpInfo.renderArea.extent.height = (uint32_t)wd_->Height;
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &wd_->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Viewport/scissor dinamicos (BasicPipeline los declara VK_DYNAMIC_STATE_*).
    VkViewport viewport{0.0f, 0.0f, (float)wd_->Width, (float)wd_->Height, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {(uint32_t)wd_->Width, (uint32_t)wd_->Height}};
    vkCmdSetViewport(fd->CommandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(fd->CommandBuffer, 0, 1, &scissor);

    return fd->CommandBuffer;
}

void SwapchainWindow::endFrame(VkDevice /*device*/, VkQueue queue, ImDrawData* imguiDrawData) {
    ImGui_ImplVulkanH_Frame* fd = &wd_->Frames[wd_->FrameIndex];

    if (imguiDrawData) {
        ImGui_ImplVulkan_RenderDrawData(imguiDrawData, fd->CommandBuffer);
    }
    vkCmdEndRenderPass(fd->CommandBuffer);

    VkSemaphore imageAcquired = wd_->FrameSemaphores[currentSemaphoreSlot_].ImageAcquiredSemaphore;
    VkSemaphore renderComplete = wd_->FrameSemaphores[currentSemaphoreSlot_].RenderCompleteSemaphore;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAcquired;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &fd->CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderComplete;

    checkVk(vkEndCommandBuffer(fd->CommandBuffer));
    checkVk(vkQueueSubmit(queue, 1, &submitInfo, fd->Fence));

    if (swapChainRebuild_) return; // el frame siguiente reconstruye antes de presentar

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderComplete;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &wd_->Swapchain;
    presentInfo.pImageIndices = &wd_->FrameIndex;
    VkResult err = vkQueuePresentKHR(queue, &presentInfo);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        swapChainRebuild_ = true;
        return;
    }
    checkVk(err);
}

void SwapchainWindow::setClearColor(float r, float g, float b, float a) {
    wd_->ClearValue.color.float32[0] = r;
    wd_->ClearValue.color.float32[1] = g;
    wd_->ClearValue.color.float32[2] = b;
    wd_->ClearValue.color.float32[3] = a;
}

VkRenderPass SwapchainWindow::renderPass() const { return wd_->RenderPass; }
uint32_t SwapchainWindow::imageCount() const { return wd_->ImageCount; }
int SwapchainWindow::width() const { return wd_->Width; }
int SwapchainWindow::height() const { return wd_->Height; }

} // namespace iron3d
