#include "../toy2d/Context.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace toy2d {

    std::unique_ptr<Context> Context::instance_ = nullptr;

    void Context::Init() {
        instance_.reset(new Context);
    }

    void Context::Quit() {
        instance_.reset(nullptr);
    }

    Context& Context::GetInstance() {
        if (!instance_) {
            Init();
        }
        return *instance_;
    }

    Context::Context() {
        CreateInstance();
        setupDebugUtilsMessenger();
        pickupPhyiscalDevice();
        queryQueueFamilyIndices();
        createDevice();
        getQueues();
    }

    Context::~Context() {
        device.destroy();
        destroyDebugUtilsMessenger();
        instance.destroy();
    }

    void Context::CreateInstance() {
        vk::InstanceCreateInfo createInfo;
        std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
        std::vector<const char*> extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        // auto layers = vk::enumerateInstanceLayerProperties();
        // for (const auto& layer : layers) {
        //     std::cout << layer.layerName << std::endl;
        // }
    
        vk::ApplicationInfo appInfo;
        appInfo.setPApplicationName("Toy2D")
               .setApplicationVersion(VK_API_VERSION_1_4);

        createInfo.setPApplicationInfo(&appInfo)
                  .setPEnabledLayerNames(layers)
                  .setPEnabledExtensionNames(extensions);
        instance = vk::createInstance(createInfo);
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL Context::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData) {
        (void)messageSeverity;
        (void)messageType;
        (void)userData;
        std::cerr << "[Vulkan Validation] " << callbackData->pMessage << '\n';
        return VK_FALSE;
    }

    void Context::setupDebugUtilsMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
        createInfo.pUserData = nullptr;

        auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(static_cast<VkInstance>(instance), "vkCreateDebugUtilsMessengerEXT"));
        if (!createFn) {
            throw std::runtime_error("vkCreateDebugUtilsMessengerEXT not found.");
        }

        VkResult result = createFn(static_cast<VkInstance>(instance), &createInfo, nullptr, &debugMessenger);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create debug utils messenger.");
        }
    }

    void Context::destroyDebugUtilsMessenger() {
        if (debugMessenger == VK_NULL_HANDLE || !instance) {
            return;
        }

        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(static_cast<VkInstance>(instance), "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn) {
            destroyFn(static_cast<VkInstance>(instance), debugMessenger, nullptr);
        }
        debugMessenger = VK_NULL_HANDLE;
    }

    void Context::pickupPhyiscalDevice() {
        auto devices = instance.enumeratePhysicalDevices();
        if (devices.empty()) {
            throw std::runtime_error("No Vulkan physical devices found.");
        }

        phyDevice = devices.front();
        for (const auto& device : devices) {
            const auto properties = device.getProperties();
            if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                phyDevice = device;
                break;
            }
        }

        std::cout << phyDevice.getProperties().deviceName << std::endl;
    }

    void Context::createDevice() {
        vk::DeviceCreateInfo createInfo;
        vk::DeviceQueueCreateInfo queueCreateInfo;
        float priorities = 1.0;
        queueCreateInfo.setPQueuePriorities(&priorities)
                       .setQueueCount(1)
                       .setQueueFamilyIndex(queueFamilyIndices.graphicsQueue.value());
        createInfo.setQueueCreateInfos(queueCreateInfo);
        //phyDevice.enumerateDeviceExtensionProperties
        device = phyDevice.createDevice(createInfo);
    }

    void Context::queryQueueFamilyIndices() {
        auto properties = phyDevice.getQueueFamilyProperties();
        for (int i = 0; i<properties.size(); ++i) {
            const auto& property = properties[i];
            //找到图形队列家族
            if (property.queueFlags & vk::QueueFlagBits::eGraphics) {
                queueFamilyIndices.graphicsQueue = i;
                break;
            }
        }
    }

    void Context::getQueues() {
        graphicsQueue = device.getQueue(queueFamilyIndices.graphicsQueue.value(), 0);
    }
}
