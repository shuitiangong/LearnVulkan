#include <../toy2d/render_process.hpp>
#include <../toy2d/shader.hpp>
#include <../toy2d/Context.hpp>

namespace toy2d {
    void RenderProcess::InitPipeline(int width, int height) {
        vk::GraphicsPipelineCreateInfo createInfo;

        //Vertex Input
        vk::PipelineVertexInputStateCreateInfo inputState;
        createInfo.setPVertexInputState(&inputState);

        //Input Assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAsm;
        inputAsm.setPrimitiveRestartEnable(false) //图元重写
                .setTopology(vk::PrimitiveTopology::eTriangleList);
        createInfo.setPInputAssemblyState(&inputAsm);

        //Shader
        auto stages = Shader::GetInstance().GetStage();
        createInfo.setStages(stages);

        //viewport
        vk::PipelineViewportStateCreateInfo viewportState;
        vk::Viewport viewport(0, 0, width, height, 0, 1);
        viewportState.setViewports(viewport);
        // 设置裁剪区域
        vk::Rect2D rect({0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        viewportState.setScissors({rect});
        createInfo.setPViewportState(&viewportState);
        
        //Rasterization
        vk::PipelineRasterizationStateCreateInfo rastInfo;
        rastInfo.setRasterizerDiscardEnable(false)
                .setCullMode(vk::CullModeFlagBits::eBack)
                .setFrontFace(vk::FrontFace::eCounterClockwise)
                .setPolygonMode(vk::PolygonMode::eFill)
                .setLineWidth(1.0f);
        createInfo.setPRasterizationState(&rastInfo);
        
        //test -- stencil depth

        //Multisampling 设成1，不开启抗锯齿
        vk::PipelineMultisampleStateCreateInfo multisample;
        multisample.setSampleShadingEnable(false)
                   .setRasterizationSamples(vk::SampleCountFlagBits::e1);
        createInfo.setPMultisampleState(&multisample);
        
        vk::PipelineColorBlendStateCreateInfo blend;
        vk::PipelineColorBlendAttachmentState attachs;
        attachs.setBlendEnable(false)
               .setColorWriteMask(vk::ColorComponentFlagBits::eA)
               .setColorWriteMask(vk::ColorComponentFlagBits::eR)
               .setColorWriteMask(vk::ColorComponentFlagBits::eG)
               .setColorWriteMask(vk::ColorComponentFlagBits::eB);

        blend.setLogicOpEnable(false)
              .setAttachments(attachs);
        createInfo.setPColorBlendState(&blend);

        auto result = Context::GetInstance().device.createGraphicsPipeline(nullptr, createInfo);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create pipeline");
        }
        pipeline = result.value;
    }

    void RenderProcess::DestroyPipeline() {
        Context::GetInstance().device.destroyPipeline(pipeline);
    }
}
