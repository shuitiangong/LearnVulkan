#include <../toy2d/texture.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <context.hpp>
#include <buffer.hpp>
#include <memory>
#include <cmath>
#include <algorithm>

namespace toy2d {
    Texture::Texture(std::string_view filename) {
        int w;
        int h;
        int channel;
        stbi_set_flip_vertically_on_load(true);
        stbi_uc* data = stbi_load(filename.data(), &w, &h, &channel, STBI_rgb_alpha);
        if (!data) {
            throw std::runtime_error(std::string("failed to load texture image: ") + stbi_failure_reason());
        }

        size_t size = w * h * 4;
        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;

        std::unique_ptr<Buffer> buffer(new Buffer(vk::BufferUsageFlagBits::eTransferSrc,
                                       size,
                                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
                                    ));
        buffer->WriteData(data, size);   
        createImage(w, h);
        allocMemory();
        Context::Instance().device.bindImageMemory(image_.image, image_.memory, 0);

        transitionImageLayoutFromUndefine2Dst();
        transformData2Image(*buffer, w, h);
        //transitionImageLayoutFromDst2Optimal();
        generateMipmaps(w, h);
        createImageView();

        stbi_image_free(data);
    }

    Texture::~Texture() {
        auto& device = Context::Instance().device;
        if (image_.view) {
            device.destroyImageView(image_.view);
        }
        if (image_.memory) {
            device.freeMemory(image_.memory);
        }
        if (image_.image) {
            device.destroyImage(image_.image);
        }
    }

    void Texture::createImage(uint32_t w, uint32_t h) {
        image_.format = vk::Format::eR8G8B8A8Srgb;
        vk::ImageCreateInfo createInfo;
        createInfo.setImageType(vk::ImageType::e2D)
                  .setArrayLayers(1)
                  .setMipLevels(mipLevels_)
                  .setExtent({w, h, 1})
                  .setFormat(image_.format)
                  .setTiling(vk::ImageTiling::eOptimal)
                  .setInitialLayout(vk::ImageLayout::eUndefined)
                  .setUsage(
                    vk::ImageUsageFlagBits::eTransferSrc   //生成mip时，上一层要当blit的源
                    | vk::ImageUsageFlagBits::eTransferDst 
                    | vk::ImageUsageFlagBits::eSampled)
                  .setSamples(vk::SampleCountFlagBits::e1);                
        image_.image = Context::Instance().device.createImage(createInfo);          
    }

    void Texture::allocMemory() {
        auto& device = Context::Instance().device;
        vk::MemoryAllocateInfo allocInfo;

        auto requirements = device.getImageMemoryRequirements(image_.image);
        allocInfo.setAllocationSize(requirements.size);

        auto index = queryBufferMemTypeIndex(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        allocInfo.setMemoryTypeIndex(index);

        image_.memory = device.allocateMemory(allocInfo);
    }

    void Texture::transformData2Image(Buffer& buffer, uint32_t w, uint32_t h) {
        Context::Instance().commandManager->ExecuteCmd(
            Context::Instance().graphicsQueue,
            [&](vk::CommandBuffer cmdBuf) {
                vk::BufferImageCopy region;
                vk::ImageSubresourceLayers subsource;
                subsource.setAspectMask(vk::ImageAspectFlagBits::eColor)
                         .setBaseArrayLayer(0)
                         .setMipLevel(0)
                         .setLayerCount(1);
                region.setBufferImageHeight(0)
                      .setBufferOffset(0)
                      .setImageExtent({w, h, 1})
                      .setBufferRowLength(0)
                      .setImageSubresource(subsource);
                cmdBuf.copyBufferToImage(buffer.buffer, 
                                         image_.image, 
                                         vk::ImageLayout::eTransferDstOptimal,
                                         region);               
            }
        );
    }

    void Texture::transitionImageLayoutFromUndefine2Dst() {
        Context::Instance().commandManager->ExecuteCmd(
            Context::Instance().graphicsQueue,
            [&](vk::CommandBuffer cmdBuf) {
                vk::ImageMemoryBarrier barrier;
                vk::ImageSubresourceRange range;
                range.setLayerCount(1)
                     .setBaseArrayLayer(0)
                     .setLevelCount(mipLevels_)
                     .setBaseMipLevel(0)
                     .setAspectMask(vk::ImageAspectFlagBits::eColor);
                barrier.setImage(image_.image)
                       .setOldLayout(vk::ImageLayout::eUndefined)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setSubresourceRange(range);
                cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, 
                                       {}, {}, nullptr, barrier);             
            }
        );
    }

    void Texture::generateMipmaps(uint32_t w, uint32_t h) {
        auto formatProperties = Context::Instance().phyDevice.getFormatProperties(image_.format);
        //如果线性采样功能不存在，直接返回
        if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) == vk::FormatFeatureFlags{}) {
            throw std::runtime_error("Texture format does not support linear filtering");
        }

        Context::Instance().commandManager->ExecuteCmd(
            Context::Instance().graphicsQueue,
            [&](vk::CommandBuffer cmdBuf) {
                vk::ImageMemoryBarrier barrier;
                vk::ImageSubresourceRange range;
                range.setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setBaseArrayLayer(0)
                     .setLayerCount(1)
                     .setLevelCount(1);
                barrier.setImage(image_.image)
                       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setSubresourceRange(range);
                
                int32_t mipWidth = static_cast<int32_t>(w);
                int32_t mipHeight = static_cast<int32_t>(h);       
                
                for (uint32_t i = 1; i<mipLevels_; ++i) {
                    range.setBaseMipLevel(i-1);
                    barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                           .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                           .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                           .setDstAccessMask(vk::AccessFlagBits::eTransferRead);
                    
                    cmdBuf.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eTransfer,
                        {}, {}, nullptr, barrier
                    );

                    vk::ImageBlit blit;

                    vk::ImageSubresourceLayers srcSubresource;
                    srcSubresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
                                  .setMipLevel(i-1)
                                  .setBaseArrayLayer(0)
                                  .setLayerCount(1);
                    
                    vk::ImageSubresourceLayers dstSubresource;
                    dstSubresource.setAspectMask(vk::ImageAspectFlagBits::eColor)
                                  .setMipLevel(i)
                                  .setBaseArrayLayer(0)
                                  .setLayerCount(1);
                    blit.setSrcSubresource(srcSubresource)
                        .setDstSubresource(dstSubresource);

                    blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
                    blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};

                    blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
                    blit.dstOffsets[1] = vk::Offset3D{
                        mipWidth > 1 ? mipWidth / 2 : 1,
                        mipHeight > 1 ? mipHeight / 2 : 1,
                        1
                    };

                    cmdBuf.blitImage(
                        image_.image, vk::ImageLayout::eTransferSrcOptimal,
                        image_.image, vk::ImageLayout::eTransferDstOptimal,
                        blit,
                        vk::Filter::eLinear);

                    barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                           .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                           .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
                           .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

                    cmdBuf.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, nullptr, barrier);

                    if (mipWidth > 1) mipWidth /= 2;
                    if (mipHeight > 1) mipHeight /= 2;
                }

                range.setBaseMipLevel(mipLevels_ - 1);
                barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
                cmdBuf.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::PipelineStageFlagBits::eFragmentShader,
                    {}, {}, nullptr, barrier);
            }
        );
    }

    void Texture::transitionImageLayoutFromDst2Optimal() {
        Context::Instance().commandManager->ExecuteCmd(
            Context::Instance().graphicsQueue,
            [&](vk::CommandBuffer cmdBuf) {
                vk::ImageMemoryBarrier barrier;
                vk::ImageSubresourceRange range;
                range.setLayerCount(1)
                     .setBaseArrayLayer(0)
                     .setLevelCount(1)
                     .setBaseMipLevel(0)
                     .setAspectMask(vk::ImageAspectFlagBits::eColor);
                barrier.setImage(image_.image)
                       .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setSrcAccessMask((vk::AccessFlagBits::eTransferWrite))
                       .setDstAccessMask((vk::AccessFlagBits::eShaderRead))
                       .setSubresourceRange(range);
                cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, 
                                       {}, {}, nullptr, barrier);             
            }
        );
    }

    void Texture::createImageView() {
        vk::ImageViewCreateInfo createInfo;
        vk::ComponentMapping mapping;
        vk::ImageSubresourceRange range;
        range.setAspectMask(vk::ImageAspectFlagBits::eColor)
             .setBaseArrayLayer(0)
             .setLayerCount(1)
             .setLevelCount(mipLevels_)
             .setBaseMipLevel(0);
        createInfo.setImage(image_.image)
                  .setViewType(vk::ImageViewType::e2D)
                  .setComponents(mapping)     
                  .setFormat(image_.format)
                  .setSubresourceRange(range);
        image_.view = Context::Instance().device.createImageView(createInfo);          
    }
}
