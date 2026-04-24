#pragma once

#include <vulkan/vulkan.hpp>

namespace toy2d {

    struct ImageResource {
        vk::Image image = nullptr;
        vk::ImageView view = nullptr;
        vk::Format format = vk::Format::eUndefined;
    };

    struct AllocatedImage : ImageResource {
        vk::DeviceMemory memory = nullptr;
    };

}
