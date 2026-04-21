#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
#include <vertex.hpp>
#include <buffer.hpp>
#include <memory>

namespace toy2d {
    class Renderer final {
    public:
        Renderer(int swapchainImageCount = 3);
        ~Renderer();

        void DrawTriangle();
    private:
        int maxFlightCount_;
        int curFrame_;

        std::vector<vk::Semaphore> imageAvaliableSems_;
        std::vector<vk::Semaphore> renderFinishSems_;
        std::vector<vk::Fence> cmdAvaliableFences_;
        std::vector<vk::CommandBuffer> cmdBufs_;

        std::unique_ptr<Buffer> hostVertexBuffer_;
        std::unique_ptr<Buffer> deviceVertexBuffer_;

        void createFences();
        void createSemaphores();
        void createCmdBuffers();
        void createVertexBuffer();
        void createVertexData();
    };
}
