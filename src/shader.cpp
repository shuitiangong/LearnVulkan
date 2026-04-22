#include "shader.hpp"
#include "context.hpp"

#include <stdexcept>

namespace toy2d {

    Shader::Shader(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource) {
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
    }

    void Shader::initDescriptorSetLayouts() {
        vk::DescriptorSetLayoutCreateInfo createInfo;
        vk::DescriptorSetLayoutBinding binding;
        binding.setBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex);
        createInfo.setBindings(binding);
        layouts_.push_back(Context::Instance().device.createDescriptorSetLayout(createInfo));

        binding.setBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment);
        createInfo.setBindings(binding);
        layouts_.push_back(Context::Instance().device.createDescriptorSetLayout(createInfo));
    }

    vk::PushConstantRange Shader::GetPushConstantRange() const {
        vk::PushConstantRange range;
        range.setOffset(0)
             .setSize(sizeof(Mat4))
             .setStageFlags(vk::ShaderStageFlagBits::eVertex);
        return range;
    }

    Shader::~Shader() {
        auto& device = Context::Instance().device;
        for (auto& layout : layouts_) {
            device.destroyDescriptorSetLayout(layout);
        }
        layouts_.clear();
        device.destroyShaderModule(vertexModule_);
        device.destroyShaderModule(fragModule_);
    }
}
