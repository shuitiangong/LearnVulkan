#include <../toy2d/Context.hpp>
#include <../toy2d/tool.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <functional>
namespace toy2d {

    Context* Context::instance_ = nullptr;

    void Context::Init(const std::vector<const char*>& extensions, GetSurfaceCallback cb) {
        instance_ = new Context(extensions, cb);
    }

    void Context::Quit() {
        delete instance_;
    }

    Context& Context::Instance() {
        assert(instance_);
        return *instance_;
    }

    Context::Context(const std::vector<const char*>& extensions, GetSurfaceCallback cb) {
        getSurfaceCb_ = cb;

        instance = createInstance(extensions);
        if (!instance) {
            std::cout << "instance create failed" << std::endl;
            exit(1);
        }

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

    vk::Instance Context::createInstance(const std::vector<const char*>& extensions) {
        vk::InstanceCreateInfo info; 
        vk::ApplicationInfo appInfo;
        std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
        std::vector<const char*> enabledExtensions = extensions;
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        appInfo.setPApplicationName("Toy2D")
               .setApplicationVersion(1)
               .setApiVersion(VK_API_VERSION_1_4);
        info.setPApplicationInfo(&appInfo)
            .setPEnabledLayerNames(layers)
            .setPEnabledExtensionNames(enabledExtensions);

        return vk::createInstance(info);
    }

    vk::PhysicalDevice Context::pickupPhysicalDevice() {
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
        return phyDevice;
    }

    vk::Device Context::createDevice(vk::SurfaceKHR surface) {
        vk::DeviceCreateInfo deviceCreateInfo;
        queryQueueInfo(surface);

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        float priorities = 1.0;
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.setPQueuePriorities(&priorities)
                       .setQueueCount(1)
                       .setQueueFamilyIndex(queueInfo.graphicsIndex.value());
        queueCreateInfos.push_back(queueCreateInfo); 
        // Check whether a separate present queue is needed.
        // If graphics and present queues differ, create both queue infos.
        if (queueInfo.graphicsIndex.value() != queueInfo.presentIndex.value()) {
            queueCreateInfo.setPQueuePriorities(&priorities)
                           .setQueueCount(1)
                           .setQueueFamilyIndex(queueInfo.presentIndex.value());
            queueCreateInfos.push_back(queueCreateInfo);               
        }
        
        std::array extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        deviceCreateInfo.setQueueCreateInfos(queueCreateInfos)
                        .setPEnabledExtensionNames(extensions);
        return phyDevice.createDevice(deviceCreateInfo);
    }

    void Context::queryQueueInfo(vk::SurfaceKHR surface) {
        auto properties = phyDevice.getQueueFamilyProperties();
        for (std::size_t i = 0; i < properties.size(); ++i) {
            const auto& property = properties[i];
            // graphics queue
            if (property.queueFlags & vk::QueueFlagBits::eGraphics) {
                queueInfo.graphicsIndex = static_cast<uint32_t>(i);
            }
            // present queue
            if (phyDevice.getSurfaceSupportKHR(i, surface)) {
                queueInfo.presentIndex = static_cast<uint32_t>(i);
            }
            if (queueInfo.graphicsIndex.has_value() && queueInfo.presentIndex) {
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
        auto vertexSource = ReadSpvFile("shader/shader.vert.spv");
        auto fragSource = ReadSpvFile("shader/shader.frag.spv");
        renderProcess->RecreateGraphicsPipeline(vertexSource, fragSource);
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL Context::DebugCallback (
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

    void Context::initCommandPool() {
        commandManager = std::make_unique<CommandManager>();
    }

    Context::~Context() {
        commandManager.reset();
        renderProcess.reset();
        swapchain.reset();
        device.destroy();
        instance.destroy();
    }
}
