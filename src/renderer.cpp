#include "renderer.hpp"
#include "math.hpp"
#include "context.hpp"

namespace toy2d {

    Renderer::Renderer(int maxFlightCount): maxFlightCount_(maxFlightCount), curFrame_(0) {
        createFences();
        createSemaphores();
        createCmdBuffers();
        createBuffers();
        createUniformBuffers(maxFlightCount);
        bufferData();
        createDescriptorPool(maxFlightCount);
        allocDescriptorSets(maxFlightCount);
        updateDescriptorSets();
        initMats();

        SetDrawColor(Color{0, 0, 0});
    }

    Renderer::~Renderer() {
        auto& device = Context::Instance().device;
        device.destroyDescriptorPool(descriptorPool_);
        verticesBuffer_.reset();
        indicesBuffer_.reset();
        mvpUniformBuffers_.clear();
        colorUniformBuffers_.clear();
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

    void Renderer::DrawRect(const Rect& rect) {
        auto& ctx = Context::Instance();
        auto& device = ctx.device;
        auto& renderProcess = ctx.renderProcess;
        auto& swapchain = ctx.swapchain;
        auto& cmdAvaliableFence = fences_[curFrame_];
        auto& imageAvaliableSem = imageAvaliableSems_[curFrame_];
        auto& cmd = cmdBufs_[curFrame_];

        if (device.waitForFences(cmdAvaliableFence, true, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) {
            throw std::runtime_error("wait for fence failed");
        }

        bufferMVPData(Mat4{});

        auto result = device.acquireNextImageKHR(
            swapchain->swapchain,
            std::numeric_limits<std::uint64_t>::max(),
            imageAvaliableSem,
            nullptr);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("wait for image in swapchain failed");
        }

        /*
            如果 acquireNextImageKHR 返回了 eErrorOutOfDateKHR、eSuboptimalKHR 或其他导致你提前 return / throw 的情况，
            那么这个 fence 不应该先被 reset 成 unsignaled；只有确认本帧会 submit 时再 reset。
         */
        device.resetFences(cmdAvaliableFence);

        auto imageIndex = result.value;
        auto& renderFinishSem = renderFinishSems_[imageIndex];
        auto& framebuffer = swapchain->framebuffers[imageIndex];

        cmd.reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmd.begin(beginInfo);
        vk::ClearValue clearValue;
        clearValue.setColor(vk::ClearColorValue(std::array<float, 4>{0.1, 0.1, 0.1, 1}));
        vk::RenderPassBeginInfo renderPassBegin;
        renderPassBegin.setRenderPass(renderProcess->renderPass)
                       .setFramebuffer(framebuffer)
                       .setClearValues(clearValue)
                       .setRenderArea(vk::Rect2D({}, swapchain->GetExtent()));
        cmd.beginRenderPass(&renderPassBegin, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, renderProcess->graphicsPipeline);

        vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, verticesBuffer_->buffer, offset);
        cmd.bindIndexBuffer(indicesBuffer_->buffer, 0, vk::IndexType::eUint32);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               renderProcess->layout,
                               0, mvpDescriptorSets_[curFrame_], {});
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               renderProcess->layout,
                               1, colorDescriptorSets_[curFrame_], {});

        auto model = Mat4::CreateTranslate(rect.position).Mul(Mat4::CreateScale(rect.size));
        //因为PushConstants包含在cmdbuffer里，所以这里每次都要调用
        cmd.pushConstants(renderProcess->layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(Mat4), model.GetData());                       
        cmd.drawIndexed(6, 1, 0, 0, 0);
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
                   .setSwapchains(swapchain->swapchain)
                   .setImageIndices(imageIndex);
        if (ctx.presentQueue.presentKHR(presentInfo) != vk::Result::eSuccess) {
            throw std::runtime_error("present queue execute failed");
        }

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

    void Renderer::createBuffers() {
        auto& device = Context::Instance().device;

        verticesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eVertexBuffer,
                                         sizeof(float) * 8,
                                         vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent));

        indicesBuffer_.reset(new Buffer(vk::BufferUsageFlagBits::eIndexBuffer,
                                         sizeof(std::uint32_t) * 6,
                                         vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent));
    }

    void Renderer::createUniformBuffers(int flightCount) {
        mvpUniformBuffers_.resize(flightCount);
        //            two mat4                  one color
        size_t size = sizeof(float) * 4 * 4 * 3;
        for (auto& buffer : mvpUniformBuffers_) {
            buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferSrc,
                         size,
                         vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent));
        }
        deviceMvpUniformBuffers_.resize(flightCount);
        for (auto& buffer : deviceMvpUniformBuffers_) {
            buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eUniformBuffer,
                         size,
                         vk::MemoryPropertyFlagBits::eDeviceLocal));
        }

        colorUniformBuffers_.resize(flightCount);
        size = sizeof(float) * 3;
        for (auto& buffer : colorUniformBuffers_) {
            buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferSrc,
                         size,
                         vk::MemoryPropertyFlagBits::eHostCoherent|vk::MemoryPropertyFlagBits::eHostVisible));
        }

        deviceColorUniformBuffers_.resize(flightCount);
        for (auto& buffer : deviceColorUniformBuffers_) {
            buffer.reset(new Buffer(vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eUniformBuffer,
                         size,
                         vk::MemoryPropertyFlagBits::eDeviceLocal));
        }
    }

    void Renderer::transformBuffer2Device(Buffer& src, Buffer& dst, size_t srcOffset, size_t dstOffset, size_t size) {
        auto cmdBuf = Context::Instance().commandManager->CreateOneCommandBuffer();
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuf.begin(beginInfo);
            vk::BufferCopy region;
            region.setSrcOffset(srcOffset)
                  .setDstOffset(dstOffset)
                  .setSize(size);
            cmdBuf.copyBuffer(src.buffer, dst.buffer, region);
        cmdBuf.end();

        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(cmdBuf);
        Context::Instance().graphicsQueue.submit(submitInfo);
        Context::Instance().graphicsQueue.waitIdle();
        Context::Instance().device.waitIdle();
        Context::Instance().commandManager->FreeCmd(cmdBuf);
    }

    void Renderer::bufferData() {
        bufferVertexData();
        bufferIndicesData();
    }

    void Renderer::bufferVertexData() {
        Vec vertices[] = {
            Vec{{-0.5, -0.5}},
            Vec{{0.5, -0.5}},
            Vec{{0.5, 0.5}},
            Vec{{-0.5, 0.5}},
        };
        verticesBuffer_->WriteData(vertices, sizeof(vertices));
    }

    void Renderer::bufferIndicesData() {
        std::uint32_t indices[] = {
            0, 1, 3,
            1, 2, 3,
        };
        indicesBuffer_->WriteData(indices, sizeof(indices));
    }

    void Renderer::bufferMVPData(const Mat4& model) {
        MVP mvp;
        mvp.project = projectMat_;
        mvp.view = viewMat_;
        mvp.model = model;
        for (int i = 0; i < mvpUniformBuffers_.size(); i++) {
            auto& buffer = mvpUniformBuffers_[i];
            buffer->WriteData(&mvp, sizeof(mvp));
            transformBuffer2Device(*buffer, *deviceMvpUniformBuffers_[i], 0, 0, buffer->size);
        }
    }

    void Renderer::SetDrawColor(const Color& color) {
        for (int i = 0; i < colorUniformBuffers_.size(); i++) {
            auto& buffer = colorUniformBuffers_[i];
            buffer->WriteData(&color, sizeof(float) * 3);

            transformBuffer2Device(*buffer, *deviceColorUniformBuffers_[i], 0, 0, buffer->size);
        }

    }

    void Renderer::initMats() {
        viewMat_ = Mat4::CreateIdentity();
        projectMat_ = Mat4::CreateIdentity();
    }

    void Renderer::SetProject(int right, int left, int bottom, int top, int far, int near) {
        projectMat_ = Mat4::CreateOrtho(left, right, top, bottom, near, far);
    }

    void Renderer::createDescriptorPool(int flightCount) {
        vk::DescriptorPoolCreateInfo createInfo;
        vk::DescriptorPoolSize size;
        size.setDescriptorCount(flightCount * 2)
            .setType(vk::DescriptorType::eUniformBuffer);
        createInfo.setPoolSizes(size)
    			  .setMaxSets(flightCount * 2);
        descriptorPool_ = Context::Instance().device.createDescriptorPool(createInfo);
    }

    void Renderer::allocDescriptorSets(int flightCount) {
        std::vector layouts(flightCount, Context::Instance().shader->GetDescriptorSetLayouts()[0]);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(descriptorPool_)
    			 .setSetLayouts(layouts);
        mvpDescriptorSets_ = Context::Instance().device.allocateDescriptorSets(allocInfo);

        layouts = std::vector(flightCount, Context::Instance().shader->GetDescriptorSetLayouts()[1]);
        allocInfo.setDescriptorPool(descriptorPool_)
    			 .setSetLayouts(layouts);
        colorDescriptorSets_ = Context::Instance().device.allocateDescriptorSets(allocInfo);
    }

    void Renderer::updateDescriptorSets() {
        for (int i = 0; i < mvpDescriptorSets_.size(); i++) {
            // bind MVP buffer
            vk::DescriptorBufferInfo bufferInfo1;
            bufferInfo1.setBuffer(deviceMvpUniformBuffers_[i]->buffer)
                       .setOffset(0)
    				   .setRange(sizeof(float) * 4 * 4 * 3);

            std::vector<vk::WriteDescriptorSet> writeInfos(2);
            writeInfos[0].setBufferInfo(bufferInfo1)
                         .setDstBinding(0)
                         .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                         .setDescriptorCount(1)
                         .setDstArrayElement(0)
                         .setDstSet(mvpDescriptorSets_[i]);

            // bind Color buffer
            vk::DescriptorBufferInfo bufferInfo2;
            bufferInfo2.setBuffer(deviceColorUniformBuffers_[i]->buffer)
                .setOffset(0)
                .setRange(sizeof(float) * 3);

            writeInfos[1].setBufferInfo(bufferInfo2)
                         .setDstBinding(0)
                         .setDstArrayElement(0)
                         .setDescriptorCount(1)
                         .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                         .setDstSet(colorDescriptorSets_[i]);

            Context::Instance().device.updateDescriptorSets(writeInfos, {});
        }
    }

}
