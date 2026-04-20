#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <optional>
#include <../toy2d/tool.hpp>
#include <../toy2d/swapchain.hpp>
#include <../toy2d/command_manager.hpp>
#include <render_process.hpp>
#include <renderer.hpp>

namespace toy2d {
    class Context final {
        public:
            using GetSurfaceCallback = std::function<vk::SurfaceKHR(vk::Instance)>;
            friend void Init(const std::vector<const char*>& extensions, GetSurfaceCallback, int, int);
            static void Init(const std::vector<const char*>& extensions, GetSurfaceCallback);
            static void Quit();
            static Context& Instance();

            struct QueueInfo {
                std::optional<uint32_t> graphicsIndex;
                std::optional<uint32_t> presentIndex;
            };

            vk::Instance instance;
            vk::PhysicalDevice phyDevice;
            vk::Device device;
            vk::Queue graphicsQueue;
            vk::Queue presentQueue;
            std::unique_ptr<Swapchain> swapchain;
            std::unique_ptr<RenderProcess> renderProcess;
            std::unique_ptr<CommandManager> commandManager;
            VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
            QueueInfo queueInfo;

        private:
            static Context* instance_;
            vk::SurfaceKHR surface_;

            GetSurfaceCallback getSurfaceCb_ = nullptr;

            Context(const std::vector<const char*>& extensions, GetSurfaceCallback);
            ~Context();

            void initRenderProcess();
            void initSwapchain(int windowWidth, int windowHeight);
            void initGraphicsPipeline();
            void initCommandPool();

            vk::Instance createInstance(const std::vector<const char*>& extensions);
            vk::PhysicalDevice pickupPhysicalDevice();
            vk::Device createDevice(vk::SurfaceKHR);
            
            void queryQueueInfo(vk::SurfaceKHR);
            void setupDebugUtilsMessenger();
            void destroyDebugUtilsMessenger();

            static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
                VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                VkDebugUtilsMessageTypeFlagsEXT messageType,
                const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                void* userData);
    };
}
