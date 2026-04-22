#pragma once

#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace toy2d {

    struct Buffer {
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* map;
        size_t size;
        size_t requireSize;

        Buffer(vk::BufferUsageFlags usage, size_t size, vk::MemoryPropertyFlags memProperty);
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

    private:
        std::uint32_t queryBufferMemTypeIndex(std::uint32_t requirementBit, vk::MemoryPropertyFlags);
    };

}
