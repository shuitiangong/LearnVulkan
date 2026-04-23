#include "buffer.hpp"
#include "context.hpp"

#include <cstring>
#include <stdexcept>

namespace toy2d {

    Buffer::Buffer(vk::BufferUsageFlags usage, size_t size, vk::MemoryPropertyFlags memProperty) {
        auto& device = Context::Instance().device;

        this->size = size;
        memProperty_ = memProperty;
        vk::BufferCreateInfo createInfo;
        createInfo.setUsage(usage)
                  .setSize(size)
                  .setSharingMode(vk::SharingMode::eExclusive);

        buffer = device.createBuffer(createInfo);

        auto requirements = device.getBufferMemoryRequirements(buffer);
        requireSize = requirements.size;
        auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, memProperty);
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.setMemoryTypeIndex(index)
                 .setAllocationSize(requirements.size);
        memory = device.allocateMemory(allocInfo);

        device.bindBufferMemory(buffer, memory, 0);

        if (memProperty & vk::MemoryPropertyFlagBits::eHostVisible) {
            map_ = device.mapMemory(memory, 0, requireSize);
        } else {
            map_ = nullptr;
        }
    }

    Buffer::~Buffer() {
        auto& device = Context::Instance().device;
        if (map_) {
            device.unmapMemory(memory);
        }
        device.freeMemory(memory);
        device.destroyBuffer(buffer);
    }

    void Buffer::WriteData(const void* data, size_t dataSize, size_t offset) {
        if (!map_) {
            throw std::runtime_error("buffer memory is not host visible");
        }
        if (!data && dataSize > 0) {
            throw std::invalid_argument("buffer write data is null");
        }
        if (offset > size || dataSize > size - offset) {
            throw std::out_of_range("buffer write range is out of bounds");
        }

        std::memcpy(static_cast<char*>(map_) + offset, data, dataSize);
        flushIfNeeded(offset, dataSize);
    }

    std::uint32_t Buffer::queryBufferMemTypeIndex(std::uint32_t type, vk::MemoryPropertyFlags flag) {
        auto property = Context::Instance().phyDevice.getMemoryProperties();

        for (std::uint32_t i = 0; i < property.memoryTypeCount; i++) {
            if ((1 << i) & type &&
                (property.memoryTypes[i].propertyFlags & flag) == flag) {
                    return i;
            }
        }

        return 0;
    }

    void Buffer::flushIfNeeded(size_t offset, size_t dataSize) {
        if (dataSize == 0 ||
            (memProperty_ & vk::MemoryPropertyFlagBits::eHostCoherent)) {
            return;
        }

        auto& ctx = Context::Instance();
        vk::DeviceSize atomSize = ctx.phyDevice.getProperties().limits.nonCoherentAtomSize;
        vk::DeviceSize flushOffset = static_cast<vk::DeviceSize>(offset);
        vk::DeviceSize flushEnd = static_cast<vk::DeviceSize>(offset + dataSize);

        flushOffset = (flushOffset / atomSize) * atomSize;
        flushEnd = ((flushEnd + atomSize - 1) / atomSize) * atomSize;
        if (flushEnd > requireSize) {
            flushEnd = requireSize;
        }

        vk::MappedMemoryRange range;
        range.setMemory(memory)
             .setOffset(flushOffset)
             .setSize(flushEnd - flushOffset);
        ctx.device.flushMappedMemoryRanges(range);
    }



}
