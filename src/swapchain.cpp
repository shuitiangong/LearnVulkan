#include "swapchain.hpp"
#include "context.hpp"
#include "buffer.hpp"

namespace toy2d {

    Swapchain::Swapchain(vk::SurfaceKHR surface, int windowWidth, int windowHeight): surface(surface) {
        querySurfaceInfo(windowWidth, windowHeight);
        swapchain = createSwapchain();
        createImageAndViews();
        createDepthResource();
    }

    Swapchain::~Swapchain() {
        auto& ctx = Context::Instance();
        for (auto& framebuffer : framebuffers) {
            Context::Instance().device.destroyFramebuffer(framebuffer);
        }

        if (depthImage.view) {
            ctx.device.destroyImageView(depthImage.view);
        }

        if (depthImage.image) {
            ctx.device.destroyImage(depthImage.image);
        }

        if (depthImage.memory) {
            ctx.device.freeMemory(depthImage.memory);
        }

        for (auto& img : swapchainImages) {
            ctx.device.destroyImageView(img.view);
        }
        
        ctx.device.destroySwapchainKHR(swapchain);
        ctx.instance.destroySurfaceKHR(surface);
    }

    void Swapchain::InitFramebuffers() {
        createFramebuffers();
    }

    void Swapchain::querySurfaceInfo(int windowWidth, int windowHeight) {
        surfaceInfo_.format = querySurfaceeFormat();

        auto capability = Context::Instance().phyDevice.getSurfaceCapabilitiesKHR(surface);
        surfaceInfo_.count = std::clamp(capability.minImageCount + 1,
                                        capability.minImageCount, capability.maxImageCount);
        surfaceInfo_.transform = capability.currentTransform;
        surfaceInfo_.extent = querySurfaceExtent(capability, windowWidth, windowHeight);
    }

    vk::SurfaceFormatKHR Swapchain::querySurfaceeFormat() {
        auto formats = Context::Instance().phyDevice.getSurfaceFormatsKHR(surface);
        for (auto& format : formats) {
            if (format.format == vk::Format::eR8G8B8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return format;
            }
        }
        return formats[0];
    }

    vk::Extent2D Swapchain::querySurfaceExtent(const vk::SurfaceCapabilitiesKHR& capability, int windowWidth, int windowHeight) {
        if (capability.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capability.currentExtent;
        } else {
            auto extent = vk::Extent2D{
                static_cast<uint32_t>(windowWidth),
                static_cast<uint32_t>(windowHeight)
            };

            extent.width = std::clamp(extent.width, capability.minImageExtent.width, capability.maxImageExtent.width);
            extent.height = std::clamp(extent.height, capability.minImageExtent.height, capability.maxImageExtent.height);
            return extent;
        }
    }

    vk::SwapchainKHR Swapchain::createSwapchain() {
        vk::SwapchainCreateInfoKHR createInfo;
        createInfo.setClipped(true)
                  .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                  .setImageExtent(surfaceInfo_.extent)
                  .setImageColorSpace(surfaceInfo_.format.colorSpace)
                  .setImageFormat(surfaceInfo_.format.format)
                  .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                  .setMinImageCount(surfaceInfo_.count)
                  .setImageArrayLayers(1)
                  .setPresentMode(vk::PresentModeKHR::eFifo)
                  .setPreTransform(surfaceInfo_.transform)
                  .setSurface(surface);

        auto& ctx = Context::Instance();
        if (ctx.queueInfo.graphicsIndex.value() == ctx.queueInfo.presentIndex.value()) {
            createInfo.setImageSharingMode(vk::SharingMode::eExclusive);
        } else {
            createInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
            std::array queueIndices = {ctx.queueInfo.graphicsIndex.value(), ctx.queueInfo.presentIndex.value()};
            createInfo.setQueueFamilyIndices(queueIndices);
        }

        return ctx.device.createSwapchainKHR(createInfo);
    }

    void Swapchain::createImageAndViews() {
        auto& ctx = Context::Instance();
        auto images = ctx.device.getSwapchainImagesKHR(swapchain);
        for (auto& image : images) {
            ImageResource img;
            img.image = image;
            img.format = surfaceInfo_.format.format;
            vk::ImageViewCreateInfo viewCreateInfo;
            vk::ImageSubresourceRange range;
            range.setBaseArrayLayer(0)
                 .setBaseMipLevel(0)
                 .setLayerCount(1)
                 .setLevelCount(1)
                 .setAspectMask(vk::ImageAspectFlagBits::eColor);
            viewCreateInfo.setImage(image)
                          .setFormat(surfaceInfo_.format.format)
                          .setViewType(vk::ImageViewType::e2D)
                          .setSubresourceRange(range)
                          .setComponents(vk::ComponentMapping{});
            img.view = ctx.device.createImageView(viewCreateInfo);
            swapchainImages.push_back(img);
        }
    }

    void Swapchain::createDepthResource() {
        auto& ctx = Context::Instance();
        
        depthImage.format = findDepthFormat();
        vk::ImageCreateInfo imageInfo;
        imageInfo.setImageType(vk::ImageType::e2D)
                 .setExtent(vk::Extent3D{GetExtent().width, GetExtent().height, 1}) // vk::Image always uses Extent3D; 2D images keep depth = 1
                 .setMipLevels(1)
                 .setArrayLayers(1)
                 .setFormat(depthImage.format)
                 .setTiling(vk::ImageTiling::eOptimal)
                 .setInitialLayout(vk::ImageLayout::eUndefined)
                 .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
                 .setSamples(vk::SampleCountFlagBits::e1)
                 .setSharingMode(vk::SharingMode::eExclusive);
        depthImage.image = ctx.device.createImage(imageInfo);
        
        auto memReq = ctx.device.getImageMemoryRequirements(depthImage.image);
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.setAllocationSize(memReq.size)
                 .setMemoryTypeIndex(queryBufferMemTypeIndex(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
        depthImage.memory = ctx.device.allocateMemory(allocInfo);
        ctx.device.bindImageMemory(depthImage.image, depthImage.memory, 0);

        // expose only the depth aspect for framebuffer and render pass attachment usage
        vk::ImageSubresourceRange range;
        range.setAspectMask(vk::ImageAspectFlagBits::eDepth)
             .setBaseMipLevel(0)
             .setLevelCount(1)
             .setBaseArrayLayer(0)
             .setLayerCount(1);

        vk::ImageViewCreateInfo viewInfo;
        viewInfo.setImage(depthImage.image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(depthImage.format)
                .setSubresourceRange(range);
        depthImage.view = ctx.device.createImageView(viewInfo);        
    }

    vk::Format Swapchain::findDepthFormat() const {
        std::array<vk::Format, 3> candidates = {
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint
        };

        auto& phy = Context::Instance().phyDevice;
        for (const auto& format : candidates) {
            auto props = phy.getFormatProperties(format);
            if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
                return format;
            }
        }

        throw std::runtime_error("Failed to find supported depth format!");
    }

    void Swapchain::createFramebuffers() {
        for (auto& img : swapchainImages) {
            vk::FramebufferCreateInfo createInfo;

            std::array<vk::ImageView, 2> attachments = {
                img.view,
                depthImage.view
            };

            createInfo.setAttachments(attachments)
                      .setLayers(1)
                      .setHeight(GetExtent().height)
                      .setWidth(GetExtent().width)
                      .setRenderPass(Context::Instance().renderProcess->renderPass);

            framebuffers.push_back(Context::Instance().device.createFramebuffer(createInfo));
        }
    }


}
