#include <../toy2d/shader.hpp>
#include <../toy2d/Context.hpp>
#include <stdexcept>

namespace toy2d {
    std::unique_ptr<Shader> Shader::instance_ = nullptr;
    void Shader::Init(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragmentSource) {
        instance_.reset(new Shader(vertexSource, fragmentSource));
    }

    void Shader::Init(const std::string& vertexPath, const std::string& fragmentPath) {
        const auto vertexSource = ReadSpvFile(vertexPath);
        const auto fragmentSource = ReadSpvFile(fragmentPath);
        if (vertexSource.empty() || fragmentSource.empty()) {
            throw std::runtime_error(
                "Failed to load SPIR-V shader files. "
                "Expected: " + vertexPath + " and " + fragmentPath
            );
        }
        Init(vertexSource, fragmentSource);
    }

    void Shader::Quit() {
        instance_.reset(nullptr);
    }

    Shader::Shader(const std::vector<uint32_t>& vertexSource, const std::vector<uint32_t>& fragmentSource) {
        if (vertexSource.empty() || fragmentSource.empty()) {
            throw std::runtime_error("Shader source is empty.");
        }

        vk::ShaderModuleCreateInfo createInfo;
        createInfo.codeSize = vertexSource.size() * sizeof(uint32_t);
        createInfo.pCode = vertexSource.data();
        vertexModule = Context::GetInstance().device.createShaderModule(createInfo);
        
        createInfo.codeSize = fragmentSource.size() * sizeof(uint32_t);
        createInfo.pCode = fragmentSource.data();
        fragmentModule = Context::GetInstance().device.createShaderModule(createInfo);

        initStage();
    }

    Shader::~Shader() {
        auto& device = Context::GetInstance().device;
        device.destroyShaderModule(vertexModule);
        device.destroyShaderModule(fragmentModule);
    }

    std::vector<vk::PipelineShaderStageCreateInfo> Shader::GetStage() {
        return stage_;
    }

    void Shader::initStage() {
        stage_.resize(2);
        stage_[0].setStage(vk::ShaderStageFlagBits::eVertex)
                   .setModule(vertexModule)
                   .setPName("main");
        stage_[1].setStage(vk::ShaderStageFlagBits::eFragment)
                   .setModule(fragmentModule)
                   .setPName("main");
    }
}
