#pragma once

#include <glm/glm.hpp>

namespace toy2d {

    struct Vertex final {
        glm::vec2 position;
        glm::vec2 texcoord;
    };

    struct Rect {
        glm::vec2 position;
        glm::vec2 size;
    };

}
