#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <vector>
#include <math.hpp>

namespace toy2d {

    class shader_program {
    public:
        shader_program(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource);
        ~shader_program();

        vk::ShaderModule GetVertexModule() const { return vertexModule_; }
        vk::ShaderModule GetFragModule() const { return fragModule_; }

        const std::vector<vk::DescriptorSetLayout>& GetDescriptorSetLayouts() const { return layouts_; }
        const std::vector<vk::VertexInputAttributeDescription>& GetVertexInputAttributeDescriptions() const { return vertexAttributes_; }
        const std::vector<vk::VertexInputBindingDescription>& GetVertexInputBindingDescriptions() const { return vertexBindings_; }
        const std::vector<vk::PushConstantRange>& GetPushConstantRanges() const { return pushConstants_; }

    private:
        vk::ShaderModule vertexModule_;
        vk::ShaderModule fragModule_;
        std::vector<vk::DescriptorSetLayout> layouts_;
        std::vector<vk::VertexInputAttributeDescription> vertexAttributes_;
        std::vector<vk::VertexInputBindingDescription> vertexBindings_;
        std::vector<vk::PushConstantRange> pushConstants_;

        void initDescriptorSetLayouts();
        void initVertexInputDescriptions();
        void initPushConstantRanges();
    };

}
