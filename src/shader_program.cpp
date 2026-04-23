#include "shader_program.hpp"
#include "context.hpp"

#include <stdexcept>

namespace toy2d {

    shader_program::shader_program(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource) {
        if (vertexSource.empty()) {
            throw std::runtime_error("create shader module failed: vertex shader source is empty");
        }
        if (fragSource.empty()) {
            throw std::runtime_error("create shader module failed: fragment shader source is empty");
        }

        vk::ShaderModuleCreateInfo vertexModuleCreateInfo, fragModuleCreateInfo;
        vertexModuleCreateInfo.codeSize = vertexSource.size() * sizeof(uint32_t);
        vertexModuleCreateInfo.pCode = vertexSource.data();
        fragModuleCreateInfo.codeSize = fragSource.size() * sizeof(uint32_t);
        fragModuleCreateInfo.pCode = fragSource.data();

        auto& device = Context::Instance().device;
        vertexModule_ = device.createShaderModule(vertexModuleCreateInfo);
        fragModule_ = device.createShaderModule(fragModuleCreateInfo);

        initDescriptorSetLayouts();
        initVertexInputDescriptions();
        initPushConstantRanges();
    }

    void shader_program::initDescriptorSetLayouts() {
        vk::DescriptorSetLayoutCreateInfo createInfo;

        //set0
        vk::DescriptorSetLayoutBinding set0Binding;
        set0Binding.setBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex);

        createInfo.setBindings(set0Binding);
        layouts_.push_back(Context::Instance().device.createDescriptorSetLayout(createInfo));

        //set1
        vk::DescriptorSetLayoutBinding set1Binding;
        set1Binding.setBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        createInfo.setBindings(set1Binding);
        layouts_.push_back(Context::Instance().device.createDescriptorSetLayout(createInfo));
    }

    void shader_program::initVertexInputDescriptions() {
        vertexAttributes_.resize(2);
        vertexAttributes_[0].setBinding(0)
                            .setFormat(vk::Format::eR32G32Sfloat)
                            .setLocation(0)
                            .setOffset(0);
        vertexAttributes_[1].setBinding(0)
                            .setFormat(vk::Format::eR32G32Sfloat)
                            .setLocation(1)
                            .setOffset(offsetof(Vertex, texcoord));

        vertexBindings_.resize(1);
        vertexBindings_[0].setBinding(0)
                          .setStride(sizeof(Vertex))
                          .setInputRate(vk::VertexInputRate::eVertex);
    }

    void shader_program::initPushConstantRanges() {
        vk::PushConstantRange range;
        range.setOffset(0)
             .setSize(sizeof(float) * 4)
             .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        pushConstants_.push_back(range);
    }

    shader_program::~shader_program() {
        auto& device = Context::Instance().device;
        for (auto& layout : layouts_) {
            device.destroyDescriptorSetLayout(layout);
        }
        layouts_.clear();
        device.destroyShaderModule(vertexModule_);
        device.destroyShaderModule(fragModule_);
    }
}
