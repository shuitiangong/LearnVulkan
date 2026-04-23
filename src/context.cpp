#include "context.hpp"

namespace toy2d {

    bool Context::IsValidationEnabled() {
#ifdef TOY2D_ENABLE_VULKAN_VALIDATION
        return true;
#else
        return false;
#endif
    }

    Context* Context::instance_ = nullptr;

    void Context::Init(std::vector<const char*>& extensions, GetSurfaceCallback cb) {
        instance_ = new Context(extensions, cb);
    }

    void Context::Quit() {
        delete instance_;
    }

    Context& Context::Instance() {
        return *instance_;
    }

    Context::Context(std::vector<const char*>& extensions, GetSurfaceCallback cb) {
        getSurfaceCb_ = cb;

        instance = createInstance(extensions);
        if (!instance) {
            std::cout << "instance create failed" << std::endl;
            exit(1);
        }
        setupDebugUtilsMessenger();

        phyDevice = pickupPhysicalDevice();
        if (!phyDevice) {
            std::cout << "pickup physical device failed" << std::endl;
            exit(1);
        }

        surface_ = getSurfaceCb_(instance);
        if (!surface_) {
            std::cout << "create surface failed" << std::endl;
            exit(1);
        }

        device = createDevice(surface_);
        if (!device) {
            std::cout << "create device failed" << std::endl;
            exit(1);
        }

        graphicsQueue = device.getQueue(queueInfo.graphicsIndex.value(), 0);
        presentQueue = device.getQueue(queueInfo.presentIndex.value(), 0);
    }

    vk::Instance Context::createInstance(std::vector<const char*>& extensions) {
        vk::InstanceCreateInfo info;
        vk::ApplicationInfo appInfo;
        appInfo.setApiVersion(VK_API_VERSION_1_3);
        std::vector<const char*> enabledExtensions = extensions;
        std::vector<const char*> layers;

        if (IsValidationEnabled()) {
            enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }

        info.setPApplicationInfo(&appInfo)
            .setPEnabledExtensionNames(enabledExtensions)
            .setPEnabledLayerNames(layers);

        return vk::createInstance(info);
    }

    vk::PhysicalDevice Context::pickupPhysicalDevice() {
        auto devices = instance.enumeratePhysicalDevices();
        if (devices.size() == 0) {
            std::cout << "you don't have suitable device to support vulkan" << std::endl;
            exit(1);
        }
        return devices[0];
    }

    vk::Device Context::createDevice(vk::SurfaceKHR surface) {
        vk::DeviceCreateInfo deviceCreateInfo;
        queryQueueInfo(surface);
        std::array extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        deviceCreateInfo.setPEnabledExtensionNames(extensions);

        std::vector<vk::DeviceQueueCreateInfo> queueInfos;
        float priority = 1;
        if (queueInfo.graphicsIndex.value() == queueInfo.presentIndex.value()) {
            vk::DeviceQueueCreateInfo queueCreateInfo;
            queueCreateInfo.setPQueuePriorities(&priority);
            queueCreateInfo.setQueueCount(1);
            queueCreateInfo.setQueueFamilyIndex(queueInfo.graphicsIndex.value());
            queueInfos.push_back(queueCreateInfo);
        } else {
            vk::DeviceQueueCreateInfo queueCreateInfo;
            queueCreateInfo.setPQueuePriorities(&priority);
            queueCreateInfo.setQueueCount(1);
            queueCreateInfo.setQueueFamilyIndex(queueInfo.graphicsIndex.value());
            queueInfos.push_back(queueCreateInfo);

            queueCreateInfo.setQueueFamilyIndex(queueInfo.presentIndex.value());
            queueInfos.push_back(queueCreateInfo);
        }
        deviceCreateInfo.setQueueCreateInfos(queueInfos);

        return phyDevice.createDevice(deviceCreateInfo);
    }

    void Context::queryQueueInfo(vk::SurfaceKHR surface) {
        auto queueProps = phyDevice.getQueueFamilyProperties();
        for (int i = 0; i < queueProps.size(); i++) {
            if (queueProps[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                queueInfo.graphicsIndex = i;
            }

            if (phyDevice.getSurfaceSupportKHR(i, surface)) {
                queueInfo.presentIndex = i;
            }

            if (queueInfo.graphicsIndex.has_value() &&
                queueInfo.presentIndex.has_value()) {
                break;
            }
        }
    }

    void Context::initSwapchain(int windowWidth, int windowHeight) {
        swapchain = std::make_unique<Swapchain>(surface_, windowWidth, windowHeight);
    }

    void Context::initRenderProcess() {
        renderProcess = std::make_unique<RenderProcess>();
    }

    void Context::initGraphicsPipeline() {
        renderProcess->RecreateGraphicsPipeline(*shaderProgram);
    }

    void Context::initCommandPool() {
        commandManager = std::make_unique<CommandManager>();
    }

    void Context::initShaderModules() {
        auto vertexSource = ReadSpvFile("shader/shader.vert.spv");
        auto fragSource = ReadSpvFile("shader/shader.frag.spv");
        shaderProgram = std::make_unique<shader_program>(vertexSource, fragSource);
    }

    Context::~Context() {
        shaderProgram.reset();
        commandManager.reset();
        renderProcess.reset();
        swapchain.reset();
        device.destroy();
        destroyDebugUtilsMessenger();
        instance.destroy();
    }

    void Context::setupDebugUtilsMessenger() {
        if (!IsValidationEnabled()) {
            return;
        }

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
        if (!IsValidationEnabled() || debugMessenger == VK_NULL_HANDLE || !instance) {
            return;
        }

        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(static_cast<VkInstance>(instance), "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn) {
            destroyFn(static_cast<VkInstance>(instance), debugMessenger, nullptr);
        }
        debugMessenger = VK_NULL_HANDLE;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL Context::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData) {
        (void)messageType;
        (void)userData;

        const char* severity = "INFO";
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            severity = "ERROR";
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            severity = "WARN";
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
            severity = "VERBOSE";
        }

        std::cerr << "[Vulkan " << severity << "] " << callbackData->pMessage << '\n';
        return VK_FALSE;
    }

}
