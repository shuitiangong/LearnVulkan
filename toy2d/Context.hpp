#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <optional>

namespace toy2d {
    class Context final {
        public:
            static void Init();
            static void Quit();
            static Context& GetInstance();

            ~Context();

            struct QueueFamilyIndices final {
                std::optional<uint32_t> graphicsQueue;
            };

            vk::Instance instance;
            vk::PhysicalDevice phyDevice;
            vk::Device device;
            vk::Queue graphicsQueue;
            VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
            QueueFamilyIndices queueFamilyIndices;
        private:
            static std::unique_ptr<Context> instance_;

            Context();

            void CreateInstance();
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
