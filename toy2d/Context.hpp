#pragma once
#include "vulkan/vulkan.hpp"
#include <memory>

namespace toy2d {
    class Context final {
        public:
            static void Init();
            static void Quit();
            static Context& GetInstance();

            ~Context();

            vk::Instance instance;
        private:
            Context();
            static std::unique_ptr<Context> instance_;
    };
}
