#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <../toy2d/tool.hpp>
#include <../toy2d/Context.hpp>
#include <../toy2d/renderer.hpp>

namespace toy2d {
    void Init(const std::vector<const char*> extensions, CreateSurfaceFunc createSurfaceFunc, int w, int h);
    void Quit();

    inline Renderer& GetRenderer() {
        return *Context::GetInstance().renderer;
    }
}
