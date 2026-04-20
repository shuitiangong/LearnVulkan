#include <../toy2d/renderer.hpp>
#include <../toy2d/Context.hpp>
#include <iostream>
#include <limits>

namespace toy2d {
    Renderer::Renderer(int maxFlightCount): maxFlightCount_(maxFlightCount), curFrame_(0) {
        createFences();
        createSemaphores();
        createCmdBuffers();
    }

    Renderer::~Renderer() {
        auto& device = Context::Instance().device;
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
        if (device.waitForFences({cmdAvaliableFences_[curFrame_]}, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
            throw std::runtime_error("wait for fence failed");
        }
        device.resetFences({cmdAvaliableFences_[curFrame_]});

        // Acquire an available swapchain image.
        auto result = device.acquireNextImageKHR(
            swapchain->swapchain,
            std::numeric_limits<uint64_t>::max(),
            imageAvaliableSems_[curFrame_]
        );
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("wait for image in swapchain failed");
        }
        auto imageIndex = result.value;

        auto& cmdMgr = ctx.commandManager;
        cmdBufs_[curFrame_].reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBufs_[curFrame_].begin(beginInfo);
        vk::ClearValue clearValue;
        clearValue.color = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);
        vk::RenderPassBeginInfo renderPassBegin;
        renderPassBegin.setRenderPass(renderProcess->renderPass)
                       .setFramebuffer(swapchain->framebuffers[imageIndex])
                       .setClearValues(clearValue)
                       .setRenderArea(vk::Rect2D({}, swapchain->GetExtent()));
        cmdBufs_[curFrame_].beginRenderPass(&renderPassBegin, vk::SubpassContents::eInline);
        cmdBufs_[curFrame_].bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.renderProcess->graphicsPipeline);
        cmdBufs_[curFrame_].draw(3, 1, 0, 0);
        cmdBufs_[curFrame_].endRenderPass();
        cmdBufs_[curFrame_].end();

        vk::SubmitInfo submit;
        vk::PipelineStageFlags flags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit.setCommandBuffers(cmdBufs_[curFrame_])
            .setWaitSemaphores(imageAvaliableSems_[curFrame_])
            .setWaitDstStageMask(flags)
            .setSignalSemaphores(renderFinishSems_[imageIndex]);
        ctx.graphicsQueue.submit(submit, cmdAvaliableFences_[curFrame_]);

        vk::PresentInfoKHR presentInfo;
        presentInfo.setWaitSemaphores(renderFinishSems_[imageIndex])
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
}
