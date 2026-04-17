#include "../toy2d/toy2d.hpp"
#include "../toy2d/Context.hpp"
#include "../toy2d/swapchain.hpp"
#include <../toy2d/shader.hpp>
#include <stdexcept>

namespace toy2d {
    void Init(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc, int w, int h) {
        Context::Init(extensions, createSurfaceFunc);
        auto& ContextInstance = Context::GetInstance();
        ContextInstance.InitSwapchain(w, h);

        Shader::Init("shader/shader.vert.spv", "shader/shader.frag.spv");
        ContextInstance.renderProcess->InitRenderPass();
        ContextInstance.renderProcess->InitLayout();
        ContextInstance.swapchain->CreateFramebuffers(w, h);
        ContextInstance.renderProcess->InitPipeline(w, h);
        ContextInstance.InitRenderer();
    }

    void Quit() {
        auto& ContextInstance = Context::GetInstance();
        ContextInstance.device.waitIdle();
        ContextInstance.renderer.reset();
        ContextInstance.renderProcess.reset();
        Shader::Quit();
        ContextInstance.DestroySwapchain();
        Context::Quit();
    }
}
