#include "../toy2d/Context.hpp"
#include <iostream>
namespace toy2d {

    std::unique_ptr<Context> Context::instance_ = nullptr;

    void Context::Init() {
        instance_.reset(new Context);
    }

    void Context::Quit() {
        instance_.reset(nullptr);
    }

    Context& Context::GetInstance() {
        if (!instance_) {
            Init();
        }
        return *instance_;
    }

    Context::Context() {
        vk::InstanceCreateInfo createInfo;
        std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
        // auto layers = vk::enumerateInstanceLayerProperties();
        // for (const auto& layer : layers) {
        //     std::cout << layer.layerName << std::endl;
        // }
    
        vk::ApplicationInfo appInfo;
        appInfo.setPApplicationName("Toy2D")
               .setApplicationVersion(VK_API_VERSION_1_4);

        createInfo.setPApplicationInfo(&appInfo)
                  .setPEnabledLayerNames(layers);
        instance = vk::createInstance(createInfo);
    }

    Context::~Context() {
        instance.destroy();
    }
}
