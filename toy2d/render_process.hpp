#pragma once

#include <vulkan/vulkan.hpp>
#include "shader_program.hpp"
#include <fstream>

namespace toy2d {

    class RenderProcess {
    public:
        vk::Pipeline graphicsPipeline = nullptr;
        vk::RenderPass renderPass = nullptr;
        vk::PipelineLayout layout = nullptr;

        RenderProcess();
        ~RenderProcess();

        void RecreateGraphicsPipeline(const shader_program& shaderProgram);
        void RecreateRenderPass();

    private:
        vk::PipelineLayout createLayout();
        vk::Pipeline createGraphicsPipeline(const shader_program& shaderProgram);
        vk::RenderPass createRenderPass();
    };

}
