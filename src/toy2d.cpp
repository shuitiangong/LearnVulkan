#include "../toy2d/toy2d.hpp"
#include "../toy2d/Context.hpp"
#include "../toy2d/swapchain.hpp"
#include <../toy2d/shader.hpp>
#include <stdexcept>

namespace toy2d {
    void Init(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc, int w, int h) {
        Context::Init(extensions, createSurfaceFunc);
        Context::GetInstance().InitSwapchain(w, h);

        Shader::Init("shader/shader.vert.spv", "shader/shader.frag.spv");
        Context::GetInstance().renderProcess->InitRenderPass();
        Context::GetInstance().renderProcess->InitLayout();
        Context::GetInstance().swapchain->CreateFramebuffers(w, h);
        Context::GetInstance().renderProcess->InitPipeline(w, h);
    }

    void Quit() {
        Context::GetInstance().renderProcess.reset();
        Shader::Quit();
        Context::GetInstance().DestroySwapchain();
        Context::Quit();
    }
}
