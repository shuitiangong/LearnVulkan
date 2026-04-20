#include "../toy2d/toy2d.hpp"
#include "../toy2d/Context.hpp"
#include "../toy2d/swapchain.hpp"
#include <stdexcept>

namespace toy2d {
    std::unique_ptr<Renderer> renderer_;

    void Init(const std::vector<const char*>& extensions, Context::GetSurfaceCallback getSurfaceCb, int windowWidth, int windowHeight) {
        Context::Init(extensions, getSurfaceCb);
        auto& ctx = Context::Instance();
        ctx.initSwapchain(windowWidth, windowHeight);
        ctx.initRenderProcess();
        ctx.initGraphicsPipeline();
        ctx.swapchain->InitFramebuffers();
        ctx.initCommandPool();

        renderer_ = std::make_unique<Renderer>(ctx.swapchain->images.size());
    }

    Renderer* GetRenderer() {
        return renderer_.get();
    }

    void Quit() {
        toy2d::Context::Instance().device.waitIdle();
        renderer_.reset();
        Context::Quit();
    }
}
