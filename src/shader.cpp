#include <../toy2d/shader.hpp>
#include <../toy2d/Context.hpp>

namespace toy2d {
    std::unique_ptr<Shader> Shader::instance_ = nullptr;
    void Shader::Init(const std::string& vertexSource, const std::string& fragmentSource) {
        instance_.reset(new Shader(vertexSource, fragmentSource));
    }

    void Shader::Quit() {
        instance_.reset(nullptr);
    }

    Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource) {
        vk::ShaderModuleCreateInfo createInfo;
        createInfo.codeSize = vertexSource.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(vertexSource.data());
        vertexModule = Context::GetInstance().device.createShaderModule(createInfo);
        
        createInfo.codeSize = fragmentSource.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(fragmentSource.data());
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
                   .setModule(Shader::GetInstance().vertexModule)
                   .setPName("main");
        stage_[1].setStage(vk::ShaderStageFlagBits::eFragment)
                   .setModule(Shader::GetInstance().fragmentModule)
                   .setPName("main");
    }
}
