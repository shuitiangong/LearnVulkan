#include <../toy2d/render_process.hpp>
#include <../toy2d/shader.hpp>
#include <../toy2d/Context.hpp>
#include <../toy2d/swapchain.hpp>
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
                .setFrontFace(vk::FrontFace::eClockwise)
                .setPolygonMode(vk::PolygonMode::eFill)
                .setLineWidth(1.0f);
        createInfo.setPRasterizationState(&rastInfo);
        
        //test -- stencil depth

        //Multisampling 设成1，不开启抗锯齿
        vk::PipelineMultisampleStateCreateInfo multisample;
        multisample.setSampleShadingEnable(false)
                   .setRasterizationSamples(vk::SampleCountFlagBits::e1);
        createInfo.setPMultisampleState(&multisample);
        
        //Color Blending
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

        //Render Pass
        createInfo.setRenderPass(renderPass)
                  .setLayout(layout);

        auto result = Context::GetInstance().device.createGraphicsPipeline(nullptr, createInfo);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create pipeline");
        }
        pipeline = result.value;
    }

    void RenderProcess::InitLayout() {
        vk::PipelineLayoutCreateInfo createInfo;
        layout = Context::GetInstance().device.createPipelineLayout(createInfo);
    }

    void RenderProcess::InitRenderPass() {
        vk::RenderPassCreateInfo createInfo;
        vk::AttachmentDescription attachDesc;
        attachDesc.setFormat(Context::GetInstance().swapchain->info.format.format)
                  .setInitialLayout(vk::ImageLayout::eUndefined)
                  .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
                  .setLoadOp(vk::AttachmentLoadOp::eClear)
                  .setStoreOp(vk::AttachmentStoreOp::eStore)
                  .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                  .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                  .setSamples(vk::SampleCountFlagBits::e1);
        createInfo.setAttachments(attachDesc);

        //对上面attachDesc进行颜色引用
        vk::AttachmentReference reference;
        reference.setLayout(vk::ImageLayout::eColorAttachmentOptimal)
                 .setAttachment(0);
        vk::SubpassDescription subpass;
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
               .setColorAttachments(reference);
        createInfo.setSubpasses(subpass);

        vk::SubpassDependency dependency;
        dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
                  .setDstSubpass(0)
                  .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                  .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
                  .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        createInfo.setDependencies(dependency);

        renderPass = Context::GetInstance().device.createRenderPass(createInfo);
    }

    RenderProcess::~RenderProcess() {
        auto& device = Context::GetInstance().device;
        device.destroyRenderPass(renderPass);
        device.destroyPipelineLayout(layout);
        device.destroyPipeline(pipeline);
    }
}
