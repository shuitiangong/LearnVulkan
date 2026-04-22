#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <vector>
#include <math.hpp>

namespace toy2d {

    class Shader {
    public:
        Shader(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragSource);
        ~Shader();

        vk::ShaderModule GetVertexModule() const { return vertexModule_; }
        vk::ShaderModule GetFragModule() const { return fragModule_; }

        const std::vector<vk::DescriptorSetLayout>& GetDescriptorSetLayouts() const { return layouts_; }
        vk::PushConstantRange GetPushConstantRange() const;

    private:
        vk::ShaderModule vertexModule_;
        vk::ShaderModule fragModule_;
        std::vector<vk::DescriptorSetLayout> layouts_;

        void initDescriptorSetLayouts();
    };

}
