#include "tool.hpp"

#include <stdexcept>

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

    std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath) {
        if (relativePath.empty()) {
            throw std::runtime_error("asset path is empty");
        }

        if (relativePath.is_absolute()) {
            if (std::filesystem::exists(relativePath)) {
                return std::filesystem::weakly_canonical(relativePath);
            }
            throw std::runtime_error("asset does not exist: " + relativePath.string());
        }

        auto current = std::filesystem::current_path();
        while (true) {
            auto candidate = current / relativePath;
            if (std::filesystem::exists(candidate)) {
                return std::filesystem::weakly_canonical(candidate);
            }

            auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }

        throw std::runtime_error("failed to resolve asset path: " + relativePath.string());
    }
}
