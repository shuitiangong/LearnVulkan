#pragma once

#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace toy2d {

    struct Buffer {
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        size_t size;
        size_t requireSize;

        Buffer(vk::BufferUsageFlags usage, size_t size, vk::MemoryPropertyFlags memProperty);
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        void WriteData(const void* data, size_t dataSize, size_t offset = 0);

    private:
        void* map_;
        vk::MemoryPropertyFlags memProperty_;

        void flushIfNeeded(size_t offset, size_t dataSize);
    };

    std::uint32_t queryBufferMemTypeIndex(std::uint32_t requirementBit, vk::MemoryPropertyFlags);

}
