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

VkFormat pickDepthFormat(VkPhysicalDevice physicalDevice) {
    // Orden de preferencia estandar: D32 puro primero (mejor precision, no
    // todas las GPUs lo dan con stencil), D24+stencil como respaldo casi
    // universal.
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                    VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    std::fprintf(stderr, "[Iron3D] Ninguna GPU deberia llegar aqui -- sin formato de profundidad valido.\n");
    std::abort();
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
    // Fase 5b: ImGui ya nos dio un render pass/framebuffers solo-color; ahora
    // se anade el attachment de profundidad por encima (ver comentario del .h).
    createDepthImage(physicalDevice, device);
    rebuildRenderPassWithDepth(device);
    resetSemaphorePool();
}

void SwapchainWindow::destroy(VkInstance instance, VkDevice device) {
    if (!wd_) return;
    destroyDepthImage(device);
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
    // El tamano (y por tanto la imagen de profundidad) puede haber cambiado;
    // se recrea siempre junto con el render pass/framebuffers de ImGui.
    destroyDepthImage(device);
    createDepthImage(physicalDevice, device);
    rebuildRenderPassWithDepth(device);
    wd_->FrameIndex = 0;
    swapChainRebuild_ = false;
    resetSemaphorePool();
}

void SwapchainWindow::createDepthImage(VkPhysicalDevice physicalDevice, VkDevice device) {
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        depthFormat_ = pickDepthFormat(physicalDevice);
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat_;
    imageInfo.extent = {(uint32_t)wd_->Width, (uint32_t)wd_->Height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateImage(device, &imageInfo, nullptr, &depthImage_));

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, depthImage_, &memReq);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const bool typeAllowed = (memReq.memoryTypeBits & (1u << i)) != 0;
        const bool isDeviceLocal =
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        if (typeAllowed && isDeviceLocal) {
            memoryTypeIndex = i;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        std::fprintf(stderr, "[Iron3D] Sin memoria DEVICE_LOCAL valida para el buffer de profundidad.\n");
        std::abort();
    }

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    checkVk(vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory_));
    checkVk(vkBindImageMemory(device, depthImage_, depthMemory_, 0));

    const bool hasStencil =
        depthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | (hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    checkVk(vkCreateImageView(device, &viewInfo, nullptr, &depthView_));
}

void SwapchainWindow::destroyDepthImage(VkDevice device) {
    if (depthView_) {
        vkDestroyImageView(device, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_) {
        vkDestroyImage(device, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_) {
        vkFreeMemory(device, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }
}

void SwapchainWindow::rebuildRenderPassWithDepth(VkDevice device) {
    // ImGui_ImplVulkanH_CreateOrResizeWindow ya nos dejo un render pass y
    // unos framebuffers solo-color; los sustituimos aqui por una version con
    // un segundo attachment de profundidad. El attachment de color (formato/
    // indice/samples) se mantiene identico, asi que la pipeline que ya tiene
    // creada Dear ImGui para componer su UI sigue siendo valida contra este
    // nuevo render pass (reglas de compatibilidad de render pass de Vulkan).
    if (wd_->RenderPass) {
        vkDestroyRenderPass(device, wd_->RenderPass, nullptr);
        wd_->RenderPass = VK_NULL_HANDLE;
    }

    VkAttachmentDescription attachments[2]{};
    attachments[0].format = wd_->SurfaceFormat.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = wd_->ClearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = depthFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // nunca se lee despues de este frame
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;
    checkVk(vkCreateRenderPass(device, &rpInfo, nullptr, &wd_->RenderPass));

    for (uint32_t i = 0; i < wd_->ImageCount; ++i) {
        ImGui_ImplVulkanH_Frame* fd = &wd_->Frames[i];
        if (fd->Framebuffer) {
            vkDestroyFramebuffer(device, fd->Framebuffer, nullptr);
            fd->Framebuffer = VK_NULL_HANDLE;
        }
        VkImageView fbAttachments[2] = {fd->BackbufferView, depthView_};
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = wd_->RenderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments;
        fbInfo.width = (uint32_t)wd_->Width;
        fbInfo.height = (uint32_t)wd_->Height;
        fbInfo.layers = 1;
        checkVk(vkCreateFramebuffer(device, &fbInfo, nullptr, &fd->Framebuffer));
    }
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

    VkClearValue clearValues[2];
    clearValues[0] = wd_->ClearValue;
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = wd_->RenderPass;
    rpInfo.framebuffer = fd->Framebuffer;
    rpInfo.renderArea.extent.width = (uint32_t)wd_->Width;
    rpInfo.renderArea.extent.height = (uint32_t)wd_->Height;
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;
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
