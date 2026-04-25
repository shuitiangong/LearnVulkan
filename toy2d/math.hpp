#pragma once

#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

namespace toy2d {

    struct Vertex final {
        glm::vec3 position;
        glm::vec2 texcoord;

        bool operator==(const Vertex& other) const {
            return position == other.position && texcoord == other.texcoord;
        }
    };

    struct Rect {
        glm::vec2 position;
        glm::vec2 size;
    };

}

namespace std {

    template<>
    struct hash<toy2d::Vertex> {
        size_t operator()(const toy2d::Vertex& vertex) const noexcept {
            size_t h1 = hash<glm::vec3>{}(vertex.position);
            size_t h2 = hash<glm::vec2>{}(vertex.texcoord);
            return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2));
        }
    };

}
