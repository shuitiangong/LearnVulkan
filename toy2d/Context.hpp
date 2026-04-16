#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <optional>
#include <../toy2d/tool.hpp>
#include <../toy2d/swapchain.hpp>

namespace toy2d {
    class Context final {
        public:
            static void Init(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc);
            static void Quit();
            static Context& GetInstance();

            ~Context();

            struct QueueFamilyIndices final {
                std::optional<uint32_t> graphicsQueue;
                std::optional<uint32_t> presentQueue;
                operator bool () const {
                    return graphicsQueue.has_value() && presentQueue.has_value();
                }
            };

            vk::Instance instance;
            vk::PhysicalDevice phyDevice;
            vk::Device device;
            vk::Queue graphicsQueue;
            vk::Queue presentQueue;
            vk::SurfaceKHR surface;
            std::unique_ptr<Swapchain> swapchain;
            VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
            QueueFamilyIndices queueFamilyIndices;

            void InitSwapchain(int w, int h) {
                swapchain.reset(new Swapchain(w, h));
            }

            void DestroySwapchain() {
                swapchain.reset();
            }
        private:
            static std::unique_ptr<Context> instance_;

            Context(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc);

            void CreateInstance(const std::vector<const char*> extensions);
            void pickupPhyiscalDevice();
            void createDevice();
            void getQueues();
            void setupDebugUtilsMessenger();
            void destroyDebugUtilsMessenger();

            void queryQueueFamilyIndices();

            static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
                VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                VkDebugUtilsMessageTypeFlagsEXT messageType,
                const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                void* userData);
    };
}
