#pragma once
#include <vulkan/vulkan.hpp>

namespace toy2d {
    class RenderProcess final {
    public:
        vk::Pipeline graphicsPipeline = nullptr;
        vk::RenderPass renderPass = nullptr;
        vk::PipelineLayout layout = nullptr;

        RenderProcess();
        ~RenderProcess();

        void RecreateGraphicsPipeline(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource);
        void RecreateRenderPass();
    private:    
        vk::PipelineLayout createLayout();
        vk::Pipeline createGraphicsPipeline(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource);
        vk::RenderPass createRenderPass();
    };
}
