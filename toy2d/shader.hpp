#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>

namespace toy2d {
    class Shader final {
    public:
        static void Init(const std::string& vertexSource, const std::string& fragmentSource);
        static void Quit();

        static Shader& GetInstance() {
            assert(instance_);
            return *instance_;
        }

        vk::ShaderModule vertexModule;
        vk::ShaderModule fragmentModule;
    private:
        static std::unique_ptr<Shader> instance_;
        Shader(const std::string& vertexSource, const std::string& fragmentSource);
    };
}
