#include "tool.hpp"

namespace toy2d {
    std::vector<uint32_t> ReadSpvFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary|std::ios::ate);

        if (!file.is_open()) {
            std::cout << "read " << filename << " failed" << std::endl;
            return {};
        }

        auto size = file.tellg();
        if (size <= 0 || (static_cast<std::size_t>(size) % sizeof(uint32_t)) != 0) {
            std::cout << "invalid spv size: " << filename << std::endl;
            return {};
        }

        std::vector<uint32_t> content(static_cast<std::size_t>(size) / sizeof(uint32_t));
        file.seekg(0);

        file.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(size));

        return content;
    }
}
