#include "renderer.hpp"
#include "context.hpp"

#include <array>
#include <stdexcept>

namespace toy2d {

    Renderer::Renderer(int maxFlightCount)
        : maxFlightCount_(maxFlightCount),
          maxObjectCountPerFrame_(1024),
          curFrame_(0),
          objectCountInFrame_(0),
          imageIndex_(0),
          frameStarted_(false),
          alignedMvpSize_(0) {
        createFences();
        createSemaphores();
        createCmdBuffers();
        createUniformBuffers(maxFlightCount);
        createDescriptorPool(maxFlightCount);
        allocDescriptorSets(maxFlightCount);
        updateDescriptorSets();
    }

    Renderer::~Renderer() {
        auto& device = Context::Instance().device;
        device.destroyDescriptorPool(descriptorPool_);
        mvpUniformBuffers_.clear();
        for (auto& sem : imageAvaliableSems_) {
            device.destroySemaphore(sem);
        }
        for (auto& sem : renderFinishSems_) {
            device.destroySemaphore(sem);
        }
        for (auto& fence : fences_) {
            device.destroyFence(fence);
        }
    }

    void Renderer::BeginFrame() {
        if (frameStarted_) {
            throw std::runtime_error("renderer frame already started");
        }

        auto& ctx = Context::Instance();
        auto& device = ctx.device;
        auto& swapchain = ctx.swapchain;
        auto& cmdAvaliableFence = fences_[curFrame_];
        auto& imageAvaliableSem = imageAvaliableSems_[curFrame_];
        auto& cmd = currentCommandBuffer();

        if (device.waitForFences(cmdAvaliableFence, true, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) {
            throw std::runtime_error("wait for fence failed");
        }

        auto result = device.acquireNextImageKHR(
            swapchain->swapchain,
            std::numeric_limits<std::uint64_t>::max(),
            imageAvaliableSem,
            nullptr);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("wait for image in swapchain failed");
        }

        imageIndex_ = result.value;
        device.resetFences(cmdAvaliableFence);
        cmd.reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmd.begin(beginInfo);

        vk::ClearValue clearValue;
        clearValue.setColor(vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}));

        vk::RenderPassBeginInfo renderPassBegin;
        renderPassBegin.setRenderPass(ctx.renderProcess->renderPass)
                       .setFramebuffer(swapchain->framebuffers[imageIndex_])
                       .setClearValues(clearValue)
                       .setRenderArea(vk::Rect2D({}, swapchain->GetExtent()));

        cmd.beginRenderPass(&renderPassBegin, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.renderProcess->graphicsPipeline);

        objectCountInFrame_ = 0;
        frameStarted_ = true;
    }

    void Renderer::Draw(const GameObject& object) {
        if (!frameStarted_) {
            throw std::runtime_error("BeginFrame must be called before Draw");
        }
        if (!object.visible || !object.mesh || !object.material) {
            return;
        }
        if (!object.material->texture || !object.material->sampler) {
            return;
        }

        auto& ctx = Context::Instance();
        auto& cmd = currentCommandBuffer();
        auto& mesh = *object.mesh;
        auto& material = *object.material;

        mesh.Upload();
        material.EnsureGpuResources(maxFlightCount_);

        if (!mesh.vertexBuffer || !mesh.indexBuffer) {
            return;
        }

        auto dynamicOffset = bufferMVPData(object.transform.GetModelMatrix());
        std::array<std::uint32_t, 1> dynamicOffsets{dynamicOffset};
        vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, mesh.vertexBuffer->buffer, offset);
        cmd.bindIndexBuffer(mesh.indexBuffer->buffer, 0, vk::IndexType::eUint32);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               ctx.renderProcess->layout,
                               0,
                               mvpDescriptorSets_[curFrame_],
                               dynamicOffsets);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               ctx.renderProcess->layout,
                               1,
                               material.GetDescriptorSet(curFrame_),
                               {});

        ColorPushConstant colorData{material.color.x, material.color.y, material.color.z, 1.0f};
        cmd.pushConstants(ctx.renderProcess->layout,
                          vk::ShaderStageFlagBits::eFragment,
                          0,
                          sizeof(ColorPushConstant),
                          &colorData);
        cmd.drawIndexed(static_cast<std::uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
    }

    void Renderer::EndFrame() {
        if (!frameStarted_) {
            throw std::runtime_error("renderer frame has not started");
        }

        auto& ctx = Context::Instance();
        auto& cmd = currentCommandBuffer();
        auto& cmdAvaliableFence = fences_[curFrame_];
        auto& imageAvaliableSem = imageAvaliableSems_[curFrame_];
        auto& renderFinishSem = renderFinishSems_[curFrame_];

        cmd.endRenderPass();
        cmd.end();

        vk::SubmitInfo submit;
        vk::PipelineStageFlags flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit.setCommandBuffers(cmd)
              .setWaitSemaphores(imageAvaliableSem)
              .setWaitDstStageMask(flags)
              .setSignalSemaphores(renderFinishSem);
        ctx.graphicsQueue.submit(submit, cmdAvaliableFence);

        vk::PresentInfoKHR presentInfo;
        presentInfo.setWaitSemaphores(renderFinishSem)
                   .setSwapchains(ctx.swapchain->swapchain)
                   .setImageIndices(imageIndex_);
        if (ctx.presentQueue.presentKHR(presentInfo) != vk::Result::eSuccess) {
            throw std::runtime_error("present queue execute failed");
        }

        frameStarted_ = false;
        curFrame_ = (curFrame_ + 1) % maxFlightCount_;
    }

    void Renderer::createFences() {
        fences_.resize(maxFlightCount_, nullptr);

        for (auto& fence : fences_) {
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

    void Renderer::createUniformBuffers(int flightCount) {
        auto minAlignment = Context::Instance().phyDevice.getProperties().limits.minUniformBufferOffsetAlignment;
        alignedMvpSize_ = sizeof(MVP);
        if (minAlignment > 0) {
            alignedMvpSize_ = static_cast<std::uint32_t>(
                ((alignedMvpSize_ + minAlignment - 1) / minAlignment) * minAlignment);
        }

        mvpUniformBuffers_.resize(flightCount);
        auto bufferSize = static_cast<size_t>(alignedMvpSize_) * maxObjectCountPerFrame_;
        for (auto& buffer : mvpUniformBuffers_) {
            buffer = std::make_unique<Buffer>(vk::BufferUsageFlagBits::eUniformBuffer,
                                              bufferSize,
                                              vk::MemoryPropertyFlagBits::eHostVisible |
                                              vk::MemoryPropertyFlagBits::eHostCoherent);
        }
    }

    std::uint32_t Renderer::bufferMVPData(const glm::mat4& model) {
        if (objectCountInFrame_ >= maxObjectCountPerFrame_) {
            throw std::runtime_error("too many objects in one frame");
        }

        MVP mvpData;
        mvpData.project = camera_.GetProjectMatrix();
        mvpData.view = camera_.GetViewMatrix();
        mvpData.model = model;

        auto offset = alignedMvpSize_ * objectCountInFrame_;
        mvpUniformBuffers_[curFrame_]->WriteData(&mvpData, sizeof(mvpData), offset);
        ++objectCountInFrame_;
        return offset;
    }

    void Renderer::createDescriptorPool(int flightCount) {
        vk::DescriptorPoolCreateInfo createInfo;
        vk::DescriptorPoolSize size;
        size.setDescriptorCount(flightCount)
            .setType(vk::DescriptorType::eUniformBufferDynamic);
        createInfo.setPoolSizes(size)
                  .setMaxSets(flightCount);
        descriptorPool_ = Context::Instance().device.createDescriptorPool(createInfo);
    }

    void Renderer::allocDescriptorSets(int flightCount) {
        std::vector layouts(flightCount, Context::Instance().shaderProgram->GetDescriptorSetLayouts()[0]);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(descriptorPool_)
                 .setSetLayouts(layouts);
        mvpDescriptorSets_ = Context::Instance().device.allocateDescriptorSets(allocInfo);
    }

    void Renderer::updateDescriptorSets() {
        for (int i = 0; i < mvpDescriptorSets_.size(); i++) {
            vk::DescriptorBufferInfo mvpInfo;
            mvpInfo.setBuffer(mvpUniformBuffers_[i]->buffer)
                   .setOffset(0)
                   .setRange(sizeof(MVP));

            vk::WriteDescriptorSet writeInfo;
            writeInfo.setBufferInfo(mvpInfo)
                     .setDstBinding(0)
                     .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
                     .setDescriptorCount(1)
                     .setDstArrayElement(0)
                     .setDstSet(mvpDescriptorSets_[i]);

            Context::Instance().device.updateDescriptorSets(writeInfo, {});
        }
    }

    vk::CommandBuffer& Renderer::currentCommandBuffer() {
        return cmdBufs_[curFrame_];
    }

}
