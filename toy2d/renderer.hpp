#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
namespace toy2d {
    class Renderer final {
    public:
        Renderer(int maxFlightCount = 3);
        ~Renderer();

        void DrawTriangle();
    private:
        int maxFlightCount_;
        int curFrame_;

        std::vector<vk::Semaphore> imageAvaliableSems_;
        std::vector<vk::Semaphore> renderFinishSems_;
        std::vector<vk::Fence> cmdAvaliableFences_;
        std::vector<vk::CommandBuffer> cmdBufs_;

        void createFences();
        void createSemaphores();
        void createCmdBuffers();
    };
}
