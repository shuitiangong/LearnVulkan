#pragma once

#include <vulkan/vulkan.hpp>
#include "context.hpp"
#include "command_manager.hpp"
#include "swapchain.hpp"
#include "math.hpp"
#include "buffer.hpp"
#include <limits>

namespace toy2d {

    class Renderer {
    public:
        Renderer(int maxFlightCount = 2);
        ~Renderer();

        void SetProject(int right, int left, int bottom, int top, int far, int near);
        void DrawRect(const Rect&);
        void SetDrawColor(const Color&);

    private:
        struct MVP {
            Mat4 project;
            Mat4 view;
            Mat4 model;
        };

        int maxFlightCount_;
        int curFrame_;
        std::vector<vk::Fence> fences_;
        std::vector<vk::Semaphore> imageAvaliableSems_;
        std::vector<vk::Semaphore> renderFinishSems_;
        std::vector<vk::CommandBuffer> cmdBufs_;
        std::unique_ptr<Buffer> verticesBuffer_;
        std::unique_ptr<Buffer> indicesBuffer_;
        Mat4 projectMat_;
        Mat4 viewMat_;
        std::vector<std::unique_ptr<Buffer>> mvpUniformBuffers_;
        std::vector<std::unique_ptr<Buffer>> colorUniformBuffers_;
        std::vector<std::unique_ptr<Buffer>> deviceMvpUniformBuffers_;
        std::vector<std::unique_ptr<Buffer>> deviceColorUniformBuffers_;
        vk::DescriptorPool descriptorPool_;
        std::vector<vk::DescriptorSet> mvpDescriptorSets_;
        std::vector<vk::DescriptorSet> colorDescriptorSets_;

        void createFences();
        void createSemaphores();
        void createCmdBuffers();
        void createBuffers();
        void createUniformBuffers(int flightCount);
        void bufferData();
        void bufferVertexData();
        void bufferIndicesData();
        void bufferMVPData(const Mat4& model);
        void initMats();
        void createDescriptorPool(int flightCount);
        void allocDescriptorSets(int flightCount);
        void updateDescriptorSets();
        void transformBuffer2Device(Buffer& src, Buffer& dst, size_t srcOffset, size_t dstOffset, size_t size);

        std::uint32_t queryBufferMemTypeIndex(std::uint32_t, vk::MemoryPropertyFlags);
    };

}
