#pragma once
#include <vulkan/vulkan.h>
#include <buffer.hpp>
#include "image_resource.hpp"
#include <string_view>

namespace toy2d {
    class Texture final {
    public:
        Texture(std::string_view filename);
        ~Texture();

        vk::Image GetVkImage() const { return image_.image; }
        vk::ImageView GetImageView() const { return image_.view; }
        vk::Format GetFormat() const { return image_.format; }
        const AllocatedImage& GetImage() const { return image_; }
        uint32_t GetMipLevels() const {return mipLevels_; }

    private:
        AllocatedImage image_;
        uint32_t mipLevels_ = 1;

        void createImage(uint32_t w, uint32_t h);
        void allocMemory();
        void createImageView();
        uint32_t queryImageMemoryIndex();
        void transitionImageLayoutFromUndefine2Dst();
        void transitionImageLayoutFromDst2Optimal();
        void generateMipmaps(uint32_t w, uint32_t h);
        void transformData2Image(Buffer&, uint32_t w, uint32_t h);
    };
}
