#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#else
#include <SDL.h>
#include <SDL_vulkan.h>
#endif

namespace
{
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr const char* kWindowTitle = "SDL + Vulkan Minimal Demo";

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

std::string sdlError(const std::string& message)
{
    return message + ": " + SDL_GetError();
}

void initSDL()
{
#if SDL_MAJOR_VERSION >= 3
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        throw std::runtime_error(sdlError("SDL_Init failed"));
    }
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        throw std::runtime_error(sdlError("SDL_Init failed"));
    }
#endif
}

SDL_Window* createWindow()
{
#if SDL_MAJOR_VERSION >= 3
    SDL_Window* window = SDL_CreateWindow(
        kWindowTitle,
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
#else
    SDL_Window* window = SDL_CreateWindow(
        kWindowTitle,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif

    if (!window)
    {
        throw std::runtime_error(sdlError("SDL_CreateWindow failed"));
    }

    return window;
}

std::vector<const char*> getRequiredInstanceExtensions(SDL_Window* window)
{
#if SDL_MAJOR_VERSION >= 3
    (void)window;
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names || count == 0)
    {
        throw std::runtime_error(sdlError("SDL_Vulkan_GetInstanceExtensions failed"));
    }

    return {names, names + count};
#else
    unsigned int count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr) || count == 0)
    {
        throw std::runtime_error(sdlError("SDL_Vulkan_GetInstanceExtensions failed"));
    }

    std::vector<const char*> names(count);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &count, names.data()))
    {
        throw std::runtime_error(sdlError("SDL_Vulkan_GetInstanceExtensions failed"));
    }

    return names;
#endif
}

void getDrawableSize(SDL_Window* window, int& width, int& height)
{
#if SDL_MAJOR_VERSION >= 3
    if (!SDL_GetWindowSizeInPixels(window, &width, &height))
    {
        throw std::runtime_error(sdlError("SDL_GetWindowSizeInPixels failed"));
    }
#else
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
#endif
}

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapchainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanApp
{
public:
    ~VulkanApp()
    {
        cleanup();
    }

    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
    }

private:
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    bool running_ = true;
    bool framebufferResized_ = false;

private:
    void initWindow()
    {
        initSDL();
        window_ = createWindow();
    }

    void initVulkan()
    {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        printStartupSummary();
    }

    void mainLoop()
    {
        while (running_)
        {
            processEvents();

            if (framebufferResized_)
            {
                framebufferResized_ = false;
                recreateSwapchain();
            }

            SDL_Delay(16);
        }

        if (device_ != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(device_);
        }
    }

    void cleanup()
    {
        destroySwapchain();

        if (device_ != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        if (surface_ != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }

        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        if (window_)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }

        SDL_Quit();
    }

    void processEvents()
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
#if SDL_MAJOR_VERSION >= 3
            if (event.type == SDL_EVENT_QUIT)
            {
                running_ = false;
            }
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            {
                framebufferResized_ = true;
            }
#else
            if (event.type == SDL_QUIT)
            {
                running_ = false;
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      event.window.event == SDL_WINDOWEVENT_RESIZED))
            {
                framebufferResized_ = true;
            }
#endif
        }
    }

    void createInstance()
    {
        const std::vector<const char*> extensions = getRequiredInstanceExtensions(window_);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "LearnVulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateInstance failed");
        }
    }

    void createSurface()
    {
#if SDL_MAJOR_VERSION >= 3
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
        {
            throw std::runtime_error(sdlError("SDL_Vulkan_CreateSurface failed"));
        }
#else
        if (!SDL_Vulkan_CreateSurface(window_, instance_, &surface_))
        {
            throw std::runtime_error(sdlError("SDL_Vulkan_CreateSurface failed"));
        }
#endif
    }

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        QueueFamilyIndices indices;

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueCount > 0 &&
                (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                indices.graphicsFamily = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
            if (queueFamilies[i].queueCount > 0 && presentSupport == VK_TRUE)
            {
                indices.presentFamily = i;
            }

            if (indices.isComplete())
            {
                break;
            }
        }

        return indices;
    }

    [[nodiscard]] bool hasRequiredDeviceExtensions(VkPhysicalDevice device) const
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(
            device,
            nullptr,
            &extensionCount,
            availableExtensions.data());

        std::set<std::string> requiredExtensions(
            kDeviceExtensions.begin(),
            kDeviceExtensions.end());

        for (const VkExtensionProperties& extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    [[nodiscard]] SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device) const
    {
        SwapchainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
        if (formatCount > 0)
        {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                device,
                surface_,
                &formatCount,
                details.formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device,
            surface_,
            &presentModeCount,
            nullptr);
        if (presentModeCount > 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                device,
                surface_,
                &presentModeCount,
                details.presentModes.data());
        }

        return details;
    }

    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const
    {
        const QueueFamilyIndices indices = findQueueFamilies(device);
        if (!indices.isComplete())
        {
            return false;
        }

        if (!hasRequiredDeviceExtensions(device))
        {
            return false;
        }

        const SwapchainSupportDetails support = querySwapchainSupport(device);
        return !support.formats.empty() && !support.presentModes.empty();
    }

    void pickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("No Vulkan physical devices found");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices)
        {
            if (isDeviceSuitable(device))
            {
                physicalDevice_ = device;
                return;
            }
        }

        throw std::runtime_error("No suitable Vulkan device found");
    }

    void createLogicalDevice()
    {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
        std::set<uint32_t> uniqueQueueFamilies = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value(),
        };

        constexpr float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueQueueFamilies.size());

        for (uint32_t family : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = family;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateDevice failed");
        }

        vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
    }

    [[nodiscard]] static VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const VkSurfaceFormatKHR& format : availableFormats)
        {
            if ((format.format == VK_FORMAT_B8G8R8A8_SRGB ||
                 format.format == VK_FORMAT_B8G8R8A8_UNORM) &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }

        return availableFormats.front();
    }

    [[nodiscard]] static VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& availableModes)
    {
        for (VkPresentModeKHR mode : availableModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    [[nodiscard]] VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        getDrawableSize(window_, width, height);

        VkExtent2D actualExtent{};
        actualExtent.width = static_cast<uint32_t>(width);
        actualExtent.height = static_cast<uint32_t>(height);
        actualExtent.width = std::clamp(
            actualExtent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(
            actualExtent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);

        return actualExtent;
    }

    void createSwapchain()
    {
        const SwapchainSupportDetails support = querySwapchainSupport(physicalDevice_);
        const VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
        const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
        const VkExtent2D extent = chooseSwapExtent(support.capabilities);
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 &&
            imageCount > support.capabilities.maxImageCount)
        {
            imageCount = support.capabilities.maxImageCount;
        }

        const uint32_t queueFamilyIndices[] = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value(),
        };

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateSwapchainKHR failed");
        }

        uint32_t swapchainImageCount = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, nullptr);
        swapchainImages_.resize(swapchainImageCount);
        vkGetSwapchainImagesKHR(
            device_,
            swapchain_,
            &swapchainImageCount,
            swapchainImages_.data());

        swapchainImageFormat_ = surfaceFormat.format;
        swapchainExtent_ = extent;
    }

    void destroySwapchain()
    {
        swapchainImages_.clear();

        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void recreateSwapchain()
    {
        int width = 0;
        int height = 0;
        getDrawableSize(window_, width, height);

        if (width == 0 || height == 0)
        {
            return;
        }

        vkDeviceWaitIdle(device_);
        destroySwapchain();
        createSwapchain();

        std::cout << "Swapchain recreated: "
                  << swapchainExtent_.width << "x" << swapchainExtent_.height
                  << ", images = " << swapchainImages_.size() << '\n';
    }

    void printStartupSummary() const
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);

        std::cout
            << "SDL major version: " << SDL_MAJOR_VERSION << '\n'
            << "Selected GPU: " << properties.deviceName << '\n'
            << "Swapchain image count: " << swapchainImages_.size() << '\n'
            << "Swapchain extent: " << swapchainExtent_.width << "x" << swapchainExtent_.height
            << '\n'
            << "Demo initialized. Close the window to exit.\n";
    }
};
} // namespace

int main()
{
    try
    {
        VulkanApp app;
        app.run();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Fatal error: " << exception.what() << '\n';
        return 1;
    }
}
