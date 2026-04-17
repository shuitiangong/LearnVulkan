#include <../toy2d/renderer.hpp>
#include <../toy2d/Context.hpp>
#include <iostream>
#include <limits>

namespace toy2d {
    Renderer::Renderer() {
        initCmdPool();
        allocCmdBuf();
        createSems();
        createFence();
    }

    Renderer::~Renderer() {
        auto& device = Context::GetInstance().device;
        device.freeCommandBuffers(cmdPool_, cmdBuf_);
        device.destroyCommandPool(cmdPool_);
        device.destroySemaphore(imageAvaliable_);
        for (const auto& sem : imageDrawFinish_) {
            device.destroySemaphore(sem);
        }
        device.destroyFence(cmdAvaliableFence_);
    }

    void Renderer::initCmdPool() {
        vk::CommandPoolCreateInfo createInfo;
        createInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        cmdPool_ = Context::GetInstance().device.createCommandPool(createInfo);
    }

    void Renderer::allocCmdBuf() {
        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.setCommandPool(cmdPool_)
                 .setCommandBufferCount(1)
                 .setLevel(vk::CommandBufferLevel::ePrimary);
        cmdBuf_ = Context::GetInstance().device.allocateCommandBuffers(allocInfo)[0];
    }

    void Renderer::Render() {
        auto& device = Context::GetInstance().device;
        auto& renderProcess = Context::GetInstance().renderProcess;
        auto& swapchain = Context::GetInstance().swapchain;
        /*
            Semaphore - GPU internal; Queue - Queue
            Fence - CPU - GPU
        */
        // Acquire an available swapchain image.
        auto result = device.acquireNextImageKHR(
            swapchain->swapchain,
            std::numeric_limits<uint64_t>::max(),
            imageAvaliable_
        );
        if (result.result != vk::Result::eSuccess) {
            std::cout << "AcquireNextImageKHR failed: " << result.result << '\n';
        }

        auto imageIndex = result.value;
        cmdBuf_.reset();
        vk::CommandBufferBeginInfo begin;
        begin.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuf_.begin(begin);
        {
            vk::RenderPassBeginInfo renderPassBegin;
            vk::Rect2D area;
            vk::ClearValue clearValue;
            clearValue.color = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);
            area.setOffset({0, 0})
                .setExtent(swapchain->info.imageExtent);

            // Framebuffer binds concrete image views; render pass describes attachment rules.
            renderPassBegin.setRenderPass(renderProcess->renderPass)
                           .setRenderArea(area)
                           .setFramebuffer(swapchain->framebuffers[imageIndex])
                           .setClearValues(clearValue);
            cmdBuf_.beginRenderPass(renderPassBegin, {});
            {
                cmdBuf_.bindPipeline(vk::PipelineBindPoint::eGraphics, renderProcess->pipeline);
                cmdBuf_.draw(3, 1, 0, 0);
            }
            cmdBuf_.endRenderPass();
        }
        cmdBuf_.end();

        vk::SubmitInfo submit;
        constexpr vk::PipelineStageFlags waitDstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit.setCommandBuffers(cmdBuf_)
              .setWaitSemaphores(imageAvaliable_)
              .setWaitDstStageMask(waitDstStage)
              .setSignalSemaphores(imageDrawFinish_[imageIndex]);
        Context::GetInstance().graphicsQueue.submit(submit, cmdAvaliableFence_);

        vk::PresentInfoKHR present;
        present.setImageIndices(imageIndex)
               .setSwapchains(swapchain->swapchain)
               .setWaitSemaphores(imageDrawFinish_[imageIndex]);

        if (Context::GetInstance().presentQueue.presentKHR(present) != vk::Result::eSuccess) {
            std::cout << "imagee presentKHR failed: " << result.result << '\n';
        }

        if (Context::GetInstance().device.waitForFences(
            cmdAvaliableFence_, true, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess) {
            std::cout << "waitForFences failed: " << result.result << '\n';
        }

        Context::GetInstance().device.resetFences(cmdAvaliableFence_);
    }

    void Renderer::createSems() {
        vk::SemaphoreCreateInfo createInfo;
        imageAvaliable_ = Context::GetInstance().device.createSemaphore(createInfo);
        imageDrawFinish_.resize(Context::GetInstance().swapchain->images.size());
        for (auto& sem : imageDrawFinish_) {
            sem = Context::GetInstance().device.createSemaphore(createInfo);
        }
    }

    void Renderer::createFence() {
        vk::FenceCreateInfo createInfo;
        cmdAvaliableFence_ = Context::GetInstance().device.createFence(createInfo);
    }
}
