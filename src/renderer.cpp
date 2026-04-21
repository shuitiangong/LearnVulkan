#include <../toy2d/renderer.hpp>
#include <../toy2d/Context.hpp>
#include <iostream>
#include <limits>

namespace toy2d {
    const std::array<Vertex, 3> vertices = {
        Vertex{-0.8f, 0.8f},
        Vertex{0.8f, 0.8f},
        Vertex{0.0f, -0.8f}
    };

    const Unifrom uniform = {Color{1.0f, 0.0f, 0.0f}};

    Renderer::Renderer(int swapchainImageCount): maxFlightCount_(swapchainImageCount), curFrame_(0) {
        createFences();
        createSemaphores();
        createCmdBuffers();
        createVertexBuffer();
        bufferVertexData();

        createUniformBuffers();
        bufferUniformData();
        createDescriptorPool();
        allocateDescriptorSets();
        updateDescriptorSets();
    }

    Renderer::~Renderer() {
        auto& device = Context::Instance().device;
        device.destroyDescriptorPool(descriptorPool_);
        hostVertexBuffer_.reset();
        deviceVertexBuffer_.reset();
        hostUniformBuffer_.clear();
        deviceUniformBuffer_.clear();

        for (auto& sem : imageAvaliableSems_) {
            device.destroySemaphore(sem);
        }
        for (auto& sem : renderFinishSems_) {
            device.destroySemaphore(sem);
        }
        for (auto& fence : cmdAvaliableFences_) {
            device.destroyFence(fence);
        }
    }

    void Renderer::DrawTriangle() {
        auto& ctx = Context::Instance();
        auto& device = ctx.device;
        auto& renderProcess = ctx.renderProcess;
        auto& swapchain = ctx.swapchain;
        auto& cmdAvaliableFence = cmdAvaliableFences_[curFrame_];
        auto& imageAvaliableSem = imageAvaliableSems_[curFrame_];
        auto& cmdBuf = cmdBufs_[curFrame_];

        if (device.waitForFences({cmdAvaliableFence}, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
            throw std::runtime_error("wait for fence failed");
        }

        // Acquire an available swapchain image.
        auto result = device.acquireNextImageKHR(
            swapchain->swapchain,
            std::numeric_limits<uint64_t>::max(),
            imageAvaliableSem
        );
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("wait for image in swapchain failed");
        }

        /*
            如果 acquireNextImageKHR 返回了 eErrorOutOfDateKHR、eSuboptimalKHR 或其他导致你提前 return / throw 的情况
            那么这个 fence 已经被 reset 成 unsignaled，但本帧没有 submit。
         */
        device.resetFences({cmdAvaliableFence});

        auto imageIndex = result.value;
        auto& renderFinishSem = renderFinishSems_[imageIndex];
        auto& framebuffer = swapchain->framebuffers[imageIndex];

        cmdBuf.reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuf.begin(beginInfo);
        vk::ClearValue clearValue;
        clearValue.color = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);
        vk::RenderPassBeginInfo renderPassBegin;
        renderPassBegin.setRenderPass(renderProcess->renderPass)
                       .setFramebuffer(framebuffer)
                       .setClearValues(clearValue)
                       .setRenderArea(vk::Rect2D({}, swapchain->GetExtent()));
        cmdBuf.beginRenderPass(&renderPassBegin, vk::SubpassContents::eInline);
        cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.renderProcess->graphicsPipeline);
        vk::DeviceSize offset = 0;
        cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, renderProcess->layout, 0, {descriptorSets_[curFrame_]}, {});
        cmdBuf.bindVertexBuffers(0, {deviceVertexBuffer_->buffer}, offset);
        cmdBuf.draw(3, 1, 0, 0);
        cmdBuf.endRenderPass();
        cmdBuf.end();

        vk::SubmitInfo submit;
        vk::PipelineStageFlags flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit.setCommandBuffers(cmdBuf)
            .setWaitSemaphores(imageAvaliableSem)
            .setWaitDstStageMask(flags)
            .setSignalSemaphores(renderFinishSem);
        ctx.graphicsQueue.submit(submit, cmdAvaliableFence);

        vk::PresentInfoKHR presentInfo;
        presentInfo.setWaitSemaphores(renderFinishSem)
                .setSwapchains(swapchain->swapchain)
                .setImageIndices(imageIndex);
        if (ctx.presentQueue.presentKHR(presentInfo) != vk::Result::eSuccess) {
            throw std::runtime_error("present queue execute failed");
        }

        curFrame_ = (curFrame_ + 1) % maxFlightCount_;
    }

    void Renderer::createFences() {
        cmdAvaliableFences_.resize(maxFlightCount_, nullptr);

        for (auto& fence : cmdAvaliableFences_) {
            vk::FenceCreateInfo fenceCreateInfo;
            fenceCreateInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
            fence = Context::Instance().device.createFence(fenceCreateInfo);
        }
    }

    void Renderer::createSemaphores() {
        auto& device = Context::Instance().device;
        vk::SemaphoreCreateInfo info;

        imageAvaliableSems_.resize(maxFlightCount_);
        renderFinishSems_.resize(maxFlightCount_);

        for (auto& sem : imageAvaliableSems_) {
            sem = device.createSemaphore(info);
        }

        for (auto& sem : renderFinishSems_) {
            sem = device.createSemaphore(info);
        }
    }

    void Renderer::createCmdBuffers() {
        cmdBufs_.resize(maxFlightCount_);

        for (auto& cmd : cmdBufs_) {
            cmd = Context::Instance().commandManager->CreateOneCommandBuffer();
        }
    }

    void Renderer::copyBuffer(vk::Buffer& src, vk::Buffer& dst, size_t size, size_t srcOffset, size_t dstOffset) {
        auto cmdBuf = Context::Instance().commandManager->CreateOneCommandBuffer();
        vk::CommandBufferBeginInfo begin;
        begin.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuf.begin(begin); {
            vk::BufferCopy region;
            region.setSize(size)
                  .setSrcOffset(srcOffset)
                  .setDstOffset(dstOffset);
            cmdBuf.copyBuffer(src, dst, region);
        } cmdBuf.end();

        vk::SubmitInfo submit;
        submit.setCommandBuffers(cmdBuf);
        Context::Instance().graphicsQueue.submit(submit);
        Context::Instance().device.waitIdle();
        Context::Instance().commandManager->FreeCmd(cmdBuf);
    }

    void Renderer::createVertexBuffer() {
        //eHostVisible 用于主机直接访问
        hostVertexBuffer_.reset(new Buffer(sizeof(vertices),
                                       vk::BufferUsageFlagBits::eTransferSrc,
                                       vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent
                                        ));
        //手动刷新同步
        // Context::Instance().device.flushMappedMemoryRanges({hostVertexBuffer_->memory});
        //手动失效同步
        // Context::Instance().device.invalidateMappedMemoryRanges({hostVertexBuffer_->memory});

        //eDeviceLocal 用于设备直接访问
        deviceVertexBuffer_.reset(new Buffer(sizeof(vertices),
                                       vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eVertexBuffer,
                                       vk::MemoryPropertyFlagBits::eDeviceLocal
                                        ));
        
    }

    void Renderer::bufferVertexData() {
        void* ptr = Context::Instance().device.mapMemory(hostVertexBuffer_->memory, 0, hostVertexBuffer_->size);
        memcpy(ptr, vertices.data(), sizeof(vertices));
        Context::Instance().device.unmapMemory(hostVertexBuffer_->memory);

        copyBuffer(hostVertexBuffer_->buffer, deviceVertexBuffer_->buffer, sizeof(vertices), 0, 0);
    }

    void Renderer::createUniformBuffers() {
        hostUniformBuffer_.resize(maxFlightCount_);
        deviceUniformBuffer_.resize(maxFlightCount_);
        
        for (auto& buffer : hostUniformBuffer_) {
            buffer.reset(new Buffer(sizeof(uniform),
                                    vk::BufferUsageFlagBits::eTransferSrc,
                                    vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent
                        ));                     
        }

        for (auto& buffer : deviceUniformBuffer_) {
            buffer.reset(new Buffer(sizeof(uniform),
                                    vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eUniformBuffer,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal
                        ));                     
        }
    }

    void Renderer::bufferUniformData() {
        for (int i = 0; i<hostUniformBuffer_.size(); ++i) {
            auto& buffer = hostUniformBuffer_[i];
            void* ptr = Context::Instance().device.mapMemory(buffer->memory, 0, buffer->size);
            memcpy(ptr, &uniform, sizeof(uniform));
            Context::Instance().device.unmapMemory(buffer->memory);
            copyBuffer(buffer->buffer, deviceUniformBuffer_[i]->buffer, sizeof(uniform), 0, 0);
        }
    }

    void Renderer::createDescriptorPool() {
        vk::DescriptorPoolCreateInfo createInfo;
        vk::DescriptorPoolSize poolSize;
        //创建数量问题
        poolSize.setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(maxFlightCount_);
        createInfo.setMaxSets(maxFlightCount_)
                  .setPoolSizes(poolSize);
        descriptorPool_ = Context::Instance().device.createDescriptorPool(createInfo);          
    }

    void Renderer::allocateDescriptorSets() {
        std::vector<vk::DescriptorSetLayout> layouts(maxFlightCount_, Context::Instance().renderProcess->descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(descriptorPool_)
                 .setDescriptorSetCount(maxFlightCount_)
                 .setSetLayouts(layouts);

        //sets不用单独销毁，pool销毁的时候会自动销毁
        descriptorSets_ = Context::Instance().device.allocateDescriptorSets(allocInfo);
    }

    void Renderer::updateDescriptorSets() {
        for (int i = 0; i<descriptorSets_.size(); ++i) {
            auto& set = descriptorSets_[i];
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.setBuffer(deviceUniformBuffer_[i]->buffer)
                      .setOffset(0)
                      .setRange(deviceUniformBuffer_[i]->size);

            vk::WriteDescriptorSet writer;
            //buffer数据更改不用更新关联关系
            writer.setDescriptorType(vk::DescriptorType::eUniformBuffer)
                  .setBufferInfo(bufferInfo)
                  .setDstBinding(0)
                  .setDstSet(set)
                  .setDstArrayElement(0)
                  .setDescriptorCount(1);
            Context::Instance().device.updateDescriptorSets({writer}, nullptr);
        }
    }
}
