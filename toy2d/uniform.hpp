#pragma once

#include <vulkan/vulkan.hpp>

namespace toy2d {
    struct Color final {
        float r, g, b;
    };

    struct Unifrom final {
        Color color;
        static vk::DescriptorSetLayoutBinding GetBinding() {
            vk::DescriptorSetLayoutBinding binding;
            //如果vertex shader中通过out变量将uniform buffer中的数据传递给fragment shader
            //那么setStageFlags需要同时包含eVertex和eFragment
            binding.setBinding(0)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setStageFlags(vk::ShaderStageFlagBits::eFragment)
                .setDescriptorCount(1);
            return binding;
        }
    };
}
