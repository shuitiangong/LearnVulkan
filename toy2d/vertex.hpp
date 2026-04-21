#pragma once
#include <vulkan/vulkan.hpp>

namespace toy2d {
    struct Vertex final {
        float x;
        float y;

        static vk::VertexInputAttributeDescription GetAttributeDescription() {
            vk::VertexInputAttributeDescription attr;
            //虽然不是颜色，但是内存布局一致即可，s代表有符号
            attr.setBinding(0)
                .setFormat(vk::Format::eR32G32Sfloat)
                .setLocation(0)
                .setOffset(0);
            return attr;
        }

        static vk::VertexInputBindingDescription GetBindingDescription() {
            vk::VertexInputBindingDescription binding;
            binding.setBinding(0)
                .setInputRate(vk::VertexInputRate::eVertex)
                .setStride(sizeof(Vertex));
            return binding;
        }
    };
}
