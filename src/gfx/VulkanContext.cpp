#include "VulkanContext.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace iron3d {

void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[Iron3D] Vulkan error %d en: %s\n", (int)result, what);
        std::abort();
    }
}

#ifndef NDEBUG
static constexpr bool kWantValidation = true;
#else
static constexpr bool kWantValidation = false;
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[validation] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

static bool layerAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (auto& l : layers) {
        if (std::strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

void VulkanContext::init(const std::vector<const char*>& requiredInstanceExtensions) {
    // --- Instancia -----------------------------------------------------
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Iron3D Studio";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Iron3D";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = requiredInstanceExtensions;
    const bool useValidation = kWantValidation && layerAvailable("VK_LAYER_KHRONOS_validation");
    if (useValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (useValidation) layers.push_back("VK_LAYER_KHRONOS_validation");

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = (uint32_t)extensions.size();
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    instanceInfo.enabledLayerCount = (uint32_t)layers.size();
    instanceInfo.ppEnabledLayerNames = layers.data();

    vkCheck(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    if (useValidation) {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT dbgInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbgInfo.pfnUserCallback = debugCallback;
            create(instance, &dbgInfo, nullptr, &debugMessenger);
        }
    }

    // --- Dispositivo fisico ---------------------------------------------
    uint32_t gpuCount = 0;
    vkCheck(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr), "vkEnumeratePhysicalDevices");
    if (gpuCount == 0) {
        std::fprintf(stderr, "[Iron3D] Ninguna GPU con soporte Vulkan.\n");
        std::abort();
    }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());

    // Preferir discreta; si no hay, la primera que valga.
    physicalDevice = gpus[0];
    for (auto gpu : gpus) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDevice = gpu;
            break;
        }
    }
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        std::fprintf(stdout, "[Iron3D] GPU: %s\n", props.deviceName);
    }

    // --- Cola grafica (con soporte de presentacion) ----------------------
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueFamily = i;
            break;
        }
    }
    if (graphicsQueueFamily == UINT32_MAX) {
        std::fprintf(stderr, "[Iron3D] No hay cola grafica en esta GPU.\n");
        std::abort();
    }

    // --- Dispositivo logico ----------------------------------------------
    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Fase 1: sin dynamic rendering ni sync2 -- render pass clasico (lo que
    // espera ImGui_ImplVulkanH_Window) y vkQueueSubmit de toda la vida.
    // Se revisita cuando IronCompute necesite barreras finas (Fase 3).
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    vkCheck(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);

    // --- Descriptor pool para Dear ImGui -----------------------------------
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiDescriptorPool), "vkCreateDescriptorPool");
}

void VulkanContext::shutdown() {
    if (imguiDescriptorPool) vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (debugMessenger) {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy) destroy(instance, debugMessenger, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
    *this = VulkanContext{};
}

} // namespace iron3d
