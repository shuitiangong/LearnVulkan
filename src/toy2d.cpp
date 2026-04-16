#include "../toy2d/toy2d.hpp"
#include "../toy2d/Context.hpp"
#include "../toy2d/swapchain.hpp"

namespace toy2d {
    void Init(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc, int w, int h) {
        Context::Init(extensions, createSurfaceFunc);
        Context::GetInstance().InitSwapchain(w, h);
    }

    void Quit() {
        Context::GetInstance().DestroySwapchain();
        Context::Quit();
    }
}
