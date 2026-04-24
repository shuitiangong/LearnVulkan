#pragma once

#include <vulkan/vulkan.hpp>
#include "context.hpp"
#include "command_manager.hpp"
#include "swapchain.hpp"
#include "game_object.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include <limits>

namespace toy2d {

    class Renderer {
    public:
        Renderer(int maxFlightCount = 2);
        ~Renderer();

        Camera& GetCamera() { return camera_; }
        const Camera& GetCamera() const { return camera_; }
        void BeginFrame();
        void Draw(const GameObject&);
        void EndFrame();

    private:
        struct MVP {
            glm::mat4 project;
            glm::mat4 view;
            glm::mat4 model;
        };

        struct ColorPushConstant {
            float r;
            float g;
            float b;
            float a;
        };

        int maxFlightCount_;
        int maxObjectCountPerFrame_;
        int curFrame_;
        int objectCountInFrame_;
        std::uint32_t imageIndex_;
        bool frameStarted_;
        std::uint32_t alignedMvpSize_;
        std::vector<vk::Fence> fences_;
        std::vector<vk::Semaphore> imageAvaliableSems_;
        std::vector<vk::Semaphore> renderFinishSems_;
        std::vector<vk::CommandBuffer> cmdBufs_;
        Camera camera_;
        std::vector<std::unique_ptr<Buffer>> mvpUniformBuffers_;
        vk::DescriptorPool descriptorPool_;
        std::vector<vk::DescriptorSet> mvpDescriptorSets_;

        void createFences();
        void createSemaphores();
        void createCmdBuffers();
        void createUniformBuffers(int flightCount);
        std::uint32_t bufferMVPData(const glm::mat4& model);
        void createDescriptorPool(int flightCount);
        void allocDescriptorSets(int flightCount);
        void updateDescriptorSets();
        vk::CommandBuffer& currentCommandBuffer();
    };

}
