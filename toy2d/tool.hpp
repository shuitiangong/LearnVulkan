#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <iostream>
#include <fstream>
#include <string>

namespace toy2d {
    using CreateSurfaceFunc = std::function<vk::SurfaceKHR(vk::Instance)>;

    template<typename T, typename U>
    void RemoveNosupportedElemes(std::vector<T>& elems, const std::vector<U>& supportedElems, 
                                std::function<bool(const T&, const U&)> eq) {
        std::size_t i = 0;
        while (i < elems.size()) {
            if (std::find_if(supportedElems.begin(), supportedElems.end(), 
                [&](const U& u) {
                    return eq(elems[i], u);
                }) 
                == supportedElems.end()) {
                elems.erase(elems.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    inline std::string ReadWholeFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary|std::ios::ate);

        if (!file.is_open()) {
            std::cout << "read " << filename << " fail" << std::endl;
            return std::string{};
        }

        auto size = file.tellg();
        std::string content;
        content.resize(size);
        file.seekg(0);
        file.read(&content[0], size);
        file.close();
        return content;
    }
}
