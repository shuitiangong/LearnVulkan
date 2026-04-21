#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
#include <vertex.hpp>
#include <buffer.hpp>
#include <uniform.hpp>
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
        std::vector<std::unique_ptr<Buffer>> hostUniformBuffer_;
        std::vector<std::unique_ptr<Buffer>> deviceUniformBuffer_;

        vk::DescriptorPool descriptorPool_;
        std::vector<vk::DescriptorSet> descriptorSets_;

        void createFences();
        void createSemaphores();
        void createCmdBuffers();
        void createVertexBuffer();
        void bufferVertexData();
        void createUniformBuffers();
        void bufferUniformData();
        void createDescriptorPool();
        void allocateDescriptorSets();
        void updateDescriptorSets();

        void copyBuffer(vk::Buffer& src, vk::Buffer& dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0);
    };
}
