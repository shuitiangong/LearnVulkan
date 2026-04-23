#include "material.hpp"
#include "context.hpp"

#include <stdexcept>

namespace toy2d {
    Material::~Material() {
        destroyGpuResources();
        destroySampler();
        destroyTexture();
    }

    vk::Sampler Material::createSampler() {
        vk::SamplerCreateInfo createInfo;
        createInfo.setMagFilter(vk::Filter::eLinear)
                  .setMinFilter(vk::Filter::eLinear)
                  .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                  .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                  .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                  .setAnisotropyEnable(false)
                  .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
                  .setUnnormalizedCoordinates(false)
                  .setCompareEnable(false)
                  .setMipmapMode(vk::SamplerMipmapMode::eLinear);
        destroySampler();
        sampler = Context::Instance().device.createSampler(createInfo);
        descriptorDirty_ = true;
        return sampler;
    }
    Texture& Material::createTexture(const std::string& path) {
        destroyTexture();
        texture = std::make_unique<Texture>(path);
        if (!sampler) {
            createSampler();
        } else {
            descriptorDirty_ = true;
        }
        return *texture;
    }

    void Material::EnsureGpuResources(int flightCount) {
        if (flightCount_ == flightCount && descriptorPool_ && descriptorSets_.size() == flightCount) {
            if (descriptorDirty_) {
                updateDescriptorSets();
            }
            return;
        }

        destroyGpuResources();
        createDescriptorPool(flightCount);
        allocDescriptorSets(flightCount);
        flightCount_ = flightCount;
        updateDescriptorSets();
    }

    vk::DescriptorSet Material::GetDescriptorSet(int frameIndex) const {
        if (frameIndex < 0 || frameIndex >= static_cast<int>(descriptorSets_.size())) {
            throw std::out_of_range("material descriptor set frame index is out of range");
        }
        return descriptorSets_[frameIndex];
    }

    void Material::destroySampler() {
        if (sampler) {
            Context::Instance().device.destroySampler(sampler);
            sampler = nullptr;
        }
        descriptorDirty_ = true;
    }
    void Material::destroyTexture() {
        texture.reset();
        descriptorDirty_ = true;
    }

    void Material::createDescriptorPool(int flightCount) {
        vk::DescriptorPoolCreateInfo createInfo;
        vk::DescriptorPoolSize size;
        size.setDescriptorCount(flightCount)
            .setType(vk::DescriptorType::eCombinedImageSampler);
        createInfo.setPoolSizes(size)
                  .setMaxSets(flightCount);
        descriptorPool_ = Context::Instance().device.createDescriptorPool(createInfo);
    }

    void Material::allocDescriptorSets(int flightCount) {
        std::vector layouts(flightCount, Context::Instance().shaderProgram->GetDescriptorSetLayouts()[1]);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(descriptorPool_)
                 .setSetLayouts(layouts);
        descriptorSets_ = Context::Instance().device.allocateDescriptorSets(allocInfo);
    }

    void Material::updateDescriptorSets() {
        if (!descriptorPool_ || descriptorSets_.empty()) {
            return;
        }

        for (int i = 0; i < descriptorSets_.size(); i++) {
            vk::DescriptorImageInfo imageInfo;
            imageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                     .setImageView(texture ? texture->view : vk::ImageView{})
                     .setSampler(sampler);

            vk::WriteDescriptorSet writeInfo;
            writeInfo.setImageInfo(imageInfo)
                     .setDstBinding(0)
                     .setDstArrayElement(0)
                     .setDescriptorCount(1)
                     .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                     .setDstSet(descriptorSets_[i]);

            Context::Instance().device.updateDescriptorSets(writeInfo, {});
        }

        descriptorDirty_ = false;
    }

    void Material::destroyGpuResources() {
        if (descriptorPool_) {
            Context::Instance().device.destroyDescriptorPool(descriptorPool_);
            descriptorPool_ = nullptr;
        }
        descriptorSets_.clear();
        flightCount_ = 0;
    }
}
