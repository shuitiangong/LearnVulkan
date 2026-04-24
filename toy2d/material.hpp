#pragma once

#include <vulkan/vulkan.hpp>
#include "math.hpp"
#include "texture.hpp"
#include <memory>
#include <string>
#include <vector>

namespace toy2d {

    class Material {
    public:
        ~Material();

        std::unique_ptr<Texture> texture;
        vk::Sampler sampler = nullptr;
        glm::vec3 color{1.0f, 1.0f, 1.0f};

        vk::Sampler createSampler();
        Texture& createTexture(const std::string& path);

        void EnsureGpuResources(int flightCount);
        vk::DescriptorSet GetDescriptorSet(int frameIndex) const;

        void destroySampler();
        void destroyTexture();

    private:
        std::vector<vk::DescriptorSet> descriptorSets_;
        vk::DescriptorPool descriptorPool_ = nullptr;
        int flightCount_ = 0;
        bool descriptorDirty_ = true;

        void createDescriptorPool(int flightCount);
        void allocDescriptorSets(int flightCount);
        void updateDescriptorSets();
        void destroyGpuResources();
    };

}
