#include "toy2d.hpp"

namespace toy2d {

    std::unique_ptr<Renderer> renderer_;

    void Init(std::vector<const char*>& extensions, Context::GetSurfaceCallback cb, int windowWidth, int windowHeight) {
        Context::Init(extensions, cb);
        auto& ctx = Context::Instance();
        ctx.initSwapchain(windowWidth, windowHeight);
        ctx.initShaderModules();
        ctx.initRenderProcess();
        ctx.initGraphicsPipeline();
        ctx.swapchain->InitFramebuffers();
        ctx.initCommandPool();

        renderer_ = std::make_unique<Renderer>(static_cast<int>(ctx.swapchain->swapchainImages.size()));
        renderer_->GetCamera().SetPerspective(45.0f,
                                              static_cast<float>(windowWidth) / static_cast<float>(windowHeight),
                                              0.1f,
                                              100.0f);
    }

    void Quit() {
        Context::Instance().device.waitIdle();
        renderer_.reset();
        Context::Quit();
    }

    Renderer* GetRenderer() {
        return renderer_.get();
    }

}
