#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <../toy2d/tool.hpp>
#include <../toy2d/Context.hpp>
#include <../toy2d/renderer.hpp>

namespace toy2d {
    void Init(const std::vector<const char*>& extensions, Context::GetSurfaceCallback getSurfaceCallback, int w, int h);
    void Quit();

    Renderer* GetRenderer();
}
