#include "render_process.hpp"
#include "context.hpp"
#include "swapchain.hpp"
#include "math.hpp"

namespace toy2d {

    RenderProcess::RenderProcess() {
        layout = createLayout();
        renderPass = createRenderPass();
        graphicsPipeline = nullptr;
    }

    RenderProcess::~RenderProcess() {
        auto& ctx = Context::Instance();
        auto& device = ctx.device;
        device.destroyRenderPass(renderPass);
        device.destroyPipelineLayout(layout);
        device.destroyPipeline(graphicsPipeline);
    }

    void RenderProcess::RecreateGraphicsPipeline(const shader_program& shaderProgram) {
        if (graphicsPipeline) {
            Context::Instance().device.destroyPipeline(graphicsPipeline);
        }
        graphicsPipeline = createGraphicsPipeline(shaderProgram);
    }

    void RenderProcess::RecreateRenderPass() {
        if (renderPass) {
            Context::Instance().device.destroyRenderPass(renderPass);
        }
        renderPass = createRenderPass();
    }

    vk::PipelineLayout RenderProcess::createLayout() {
        vk::PipelineLayoutCreateInfo createInfo;
        createInfo.setSetLayouts(Context::Instance().shaderProgram->GetDescriptorSetLayouts())
                  .setPushConstantRanges(Context::Instance().shaderProgram->GetPushConstantRanges());
                  

        return Context::Instance().device.createPipelineLayout(createInfo);
    }

    vk::Pipeline RenderProcess::createGraphicsPipeline(const shader_program& shaderProgram) {
        auto& ctx = Context::Instance();

        vk::GraphicsPipelineCreateInfo createInfo;

        // 0. shader prepare
        std::array<vk::PipelineShaderStageCreateInfo, 2> stageCreateInfos;
        stageCreateInfos[0].setModule(shaderProgram.GetVertexModule())
                           .setPName("main")
                           .setStage(vk::ShaderStageFlagBits::eVertex);
        stageCreateInfos[1].setModule(shaderProgram.GetFragModule())
                           .setPName("main")
                           .setStage(vk::ShaderStageFlagBits::eFragment);

        // 1. vertex input
        vk::PipelineVertexInputStateCreateInfo vertexInputCreateInfo;
        vertexInputCreateInfo.setVertexAttributeDescriptions(shaderProgram.GetVertexInputAttributeDescriptions())
                             .setVertexBindingDescriptions(shaderProgram.GetVertexInputBindingDescriptions());

        // 2. vertex assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAsmCreateInfo;
        inputAsmCreateInfo.setPrimitiveRestartEnable(false)
                          .setTopology(vk::PrimitiveTopology::eTriangleList);

        // 3. viewport and scissor
        vk::PipelineViewportStateCreateInfo viewportInfo;
        vk::Viewport viewport(0, 0, ctx.swapchain->GetExtent().width, ctx.swapchain->GetExtent().height, 0, 1);
        vk::Rect2D scissor(vk::Rect2D({0, 0}, ctx.swapchain->GetExtent()));
        viewportInfo.setViewports(viewport)
                    .setScissors(scissor);

        // 4. rasteraizer
        vk::PipelineRasterizationStateCreateInfo rasterInfo;
        rasterInfo.setCullMode(vk::CullModeFlagBits::eBack)
                  .setFrontFace(vk::FrontFace::eCounterClockwise)
                  .setDepthClampEnable(false)
                  .setLineWidth(1)
                  .setPolygonMode(vk::PolygonMode::eFill)
                  .setRasterizerDiscardEnable(false);

        // 5. multisampler
        vk::PipelineMultisampleStateCreateInfo multisampleInfo;
        multisampleInfo.setSampleShadingEnable(false)
                       .setRasterizationSamples(vk::SampleCountFlagBits::e1);

        // 6. depth and stencil buffer
        // We currently don't need depth and stencil buffer
        vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
        depthStencilInfo.setDepthTestEnable(true)
                        .setDepthWriteEnable(true)
                        .setDepthCompareOp(vk::CompareOp::eLess)
                        .setDepthBoundsTestEnable(false)
                        .setStencilTestEnable(false);

        // 7. blending
        vk::PipelineColorBlendAttachmentState blendAttachmentState;
        blendAttachmentState.setBlendEnable(false)
                            .setColorWriteMask(vk::ColorComponentFlagBits::eA|
                                               vk::ColorComponentFlagBits::eB|
                                               vk::ColorComponentFlagBits::eG|
                                               vk::ColorComponentFlagBits::eR);
        vk::PipelineColorBlendStateCreateInfo blendInfo;
        blendInfo.setAttachments(blendAttachmentState)
                 .setLogicOpEnable(false);

        // create graphics pipeline
        createInfo.setStages(stageCreateInfos)
                  .setLayout(layout)
                  .setPVertexInputState(&vertexInputCreateInfo)
                  .setPInputAssemblyState(&inputAsmCreateInfo)
                  .setPViewportState(&viewportInfo)
                  .setPRasterizationState(&rasterInfo)
                  .setPMultisampleState(&multisampleInfo)
                  .setPDepthStencilState(&depthStencilInfo)
                  .setPColorBlendState(&blendInfo)
                  .setRenderPass(renderPass);

        auto result = ctx.device.createGraphicsPipeline(nullptr, createInfo);
        if (result.result != vk::Result::eSuccess) {
            std::cout << "create graphics pipeline failed: " << result.result << std::endl;
        }

        return result.value;
    }

    vk::RenderPass RenderProcess::createRenderPass() {
        auto& ctx = Context::Instance();

        vk::RenderPassCreateInfo createInfo;

        vk::AttachmentDescription colorAttachment;
        colorAttachment.setFormat(ctx.swapchain->GetFormat().format)
                         .setSamples(vk::SampleCountFlagBits::e1)
                         .setLoadOp(vk::AttachmentLoadOp::eClear)
                         .setStoreOp(vk::AttachmentStoreOp::eStore)
                         .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                         .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                         .setInitialLayout(vk::ImageLayout::eUndefined)
                         .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
        vk::AttachmentReference colorRef;
        colorRef.setAttachment(0)
                 .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::AttachmentDescription depthAttachment;
        depthAttachment.setFormat(ctx.swapchain->GetDepthFormat())
                         .setSamples(vk::SampleCountFlagBits::e1)
                         .setLoadOp(vk::AttachmentLoadOp::eClear)
                         .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                         .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                         .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                         .setInitialLayout(vk::ImageLayout::eUndefined)
                         .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::AttachmentReference depthRef;
        depthRef.setAttachment(1)
                 .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);         

        vk::SubpassDescription subpassDesc;
        subpassDesc.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                   .setColorAttachments(colorRef)
                   .setPDepthStencilAttachment(&depthRef);

        vk::SubpassDependency dependency;
        dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(
                vk::PipelineStageFlagBits::eColorAttachmentOutput
                | vk::PipelineStageFlagBits::eEarlyFragmentTests)
            .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                | vk::PipelineStageFlagBits::eEarlyFragmentTests)
            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite
                | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

        std::array<vk::AttachmentDescription, 2> attachments = {
            colorAttachment,
            depthAttachment
        };        

        subpassDesc.setColorAttachments(colorRef);
        createInfo.setAttachments(attachments)
                  .setSubpasses(subpassDesc)
                  .setDependencies(dependency);
                  
        return Context::Instance().device.createRenderPass(createInfo);
    }

}
