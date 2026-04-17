#include <../toy2d/renderer.hpp>
#include <../toy2d/Context.hpp>
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
        device.destroySemaphore(imageDrawFinish_);
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
        //鑾峰彇鍙互娓叉煋鐨勫浘鐗囩储寮?
        auto result = device.acquireNextImageKHR(swapchain->swapchain, 
                                                 std::numeric_limits<uint64_t>::max(),
                                                 imageAvaliable_);
        if (result.result != vk::Result::eSuccess) {
            std::cout << "AcquireNextImageKHR failed: " << result.result << '\n';
        }

        auto imageIndex = result.value;

        cmdBuf_.reset();
        vk::CommandBufferBeginInfo begin;
        begin.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuf_.begin(begin); {
            vk::RenderPassBeginInfo renderPassBegin;
            vk::Rect2D area;
            vk::ClearValue clearValue;
            clearValue.color = vk::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);
            area.setOffset({0, 0})
                .setExtent(swapchain->info.imageExtent);
            //renderprocess锛宖ramebuffer浼犵殑renderpass鍙槸閰嶇疆鍙傛暟
            //杩欓噷鍙互浼犻厤缃吋瀹癸紝浣嗗疄渚嬩笉鍚岀殑renderpass    

            renderPassBegin.setRenderPass(renderProcess->renderPass)
                           .setRenderArea(area)
                           .setFramebuffer(swapchain->framebuffers[imageIndex])
                           .setClearValues(clearValue);
            cmdBuf_.beginRenderPass(renderPassBegin, {}); {
                cmdBuf_.bindPipeline(vk::PipelineBindPoint::eGraphics, renderProcess->pipeline);
                cmdBuf_.draw(3, 1, 0, 0);
            } cmdBuf_.endRenderPass();
        } cmdBuf_.end();

        vk::SubmitInfo submit;
        submit.setCommandBuffers(cmdBuf_)
              .setWaitSemaphores(imageAvaliable_)
              .setSignalSemaphores(imageDrawFinish_);
        Context::GetInstance().graphicsQueue.submit(submit, cmdAvaliableFence_);

        vk::PresentInfoKHR present;
        present.setImageIndices(imageIndex)
               .setSwapchains(swapchain->swapchain)
               .setWaitSemaphores(imageDrawFinish_);
               
        if (Context::GetInstance().presentQueue.presentKHR(present) != vk::Result::eSuccess) {
            std::cout << "imagee presentKHR failed: " << result.result << '\n';
        }

        if (Context::GetInstance().device.waitForFences(cmdAvaliableFence_, true, std::numeric_limits<uint64_t>::max())
            != vk::Result::eSuccess) {
            std::cout << "waitForFences failed: " << result.result << '\n';
        }

        Context::GetInstance().device.resetFences(cmdAvaliableFence_);
    }

    void Renderer::createSems() {
        vk::SemaphoreCreateInfo createInfo;
        imageAvaliable_ = Context::GetInstance().device.createSemaphore(createInfo);
        imageDrawFinish_ = Context::GetInstance().device.createSemaphore(createInfo);
    }

    void Renderer::createFence() {
        vk::FenceCreateInfo createInfo;
        cmdAvaliableFence_ = Context::GetInstance().device.createFence(createInfo);
    }
}
