#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace toy2d {
    template<typename T, typename U>
    void RemoveNosupportedElemes(
        std::vector<T>& elems,
        const std::vector<U>& supportedElems,
        std::function<bool(const T&, const U&)> eq);

    std::string ReadWholeFile(const std::string& filename);

    std::vector<uint32_t> ReadSpvFile(const std::string& filename);
}

#include "tool.inl"
