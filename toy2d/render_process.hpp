#pragma once
#include <vulkan/vulkan.hpp>
#include <../toy2d/uniform.hpp>


namespace toy2d {
    class RenderProcess final {
    public:
        vk::Pipeline graphicsPipeline = nullptr;
        vk::RenderPass renderPass = nullptr;
        vk::PipelineLayout layout = nullptr;
        vk::DescriptorSetLayout descriptorSetLayout = nullptr;

        RenderProcess();
        ~RenderProcess();

        void RecreateGraphicsPipeline(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource);
        void RecreateRenderPass();
    private:    
        vk::PipelineLayout createLayout();
        vk::DescriptorSetLayout createDescriptorSetLayout();
        vk::Pipeline createGraphicsPipeline(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource);
        vk::RenderPass createRenderPass();
    };
}
