#include "../toy2d/toy2d.hpp"
#include "../toy2d/Context.hpp"
#include "../toy2d/swapchain.hpp"
#include <../toy2d/shader.hpp>

namespace toy2d {
    void Init(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc, int w, int h) {
        Context::Init(extensions, createSurfaceFunc);
        Context::GetInstance().InitSwapchain(w, h);
        Shader::Init(ReadWholeFile("shader.vert"), ReadWholeFile("shader.frag"));
        Context::GetInstance().renderProcess->InitRenderPass();
        Context::GetInstance().renderProcess->InitPipeline(w, h);
    }

    void Quit() {
        Context::GetInstance().renderProcess.reset();
        Context::GetInstance().DestroySwapchain();
        Context::Quit();
        Shader::Quit();
    }
}
