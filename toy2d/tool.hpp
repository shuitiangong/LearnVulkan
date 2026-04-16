#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>
#include <vulkan/vulkan.hpp>

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
}
